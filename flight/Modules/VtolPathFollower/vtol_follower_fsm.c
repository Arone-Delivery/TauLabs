/**
 ******************************************************************************
 * @addtogroup TauLabsModules Tau Labs Modules
 * @{
 * @addtogroup VtolPathFollower VTOL path follower module
 * @{
 *
 * @file       vtol_follower_fsm.c
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2013
 * @brief      FSMs for VTOL path navigation
 *
 * @note
 * This module contains a set of FSMs that are selected based on the @ref
 * vtol_goals that comes from @ref PathDesired. Some of those goals may be
 * simple single step actions like fly to a location and hold. However,
 * others might be more complex like when landing at home. The switchable
 * FSMs allow easily adjusting the complexity.
 *
 * The individual @ref vtol_fsm_state do not directly determine the behavior,
 * because there is a lot of redundancy between some of the states. For most
 * common behaviors (fly a path, hold a position) the ultimate behavior is
 * determined by the @ref vtol_nav_mode. When a state is entered the "enable_*"
 * method (@ref VtolNavigationEnable) that is called will configure the navigation
 * mode and the appropriate parameters, as well as configure any timeouts.
 *
 * While in a state the "do_" methods (@ref VtolNavigationDo) will actually
 * update the control signals to achieve the desired flight. The default
 * method @ref do_default will work in most cases and simply calls the
 * appropriate method based on the @ref vtol_nav_mode.
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "openpilot.h"

#include "vtol_follower_priv.h"

#include "pathdesired.h"
#include "positionactual.h"
#include "stabilizationdesired.h"
#include "vtolpathfollowerstatus.h"

// Various navigation constants
const static float RTH_MIN_ALTITUDE = 15;    //!< Hover at least 15 m above home */
const static float RTH_VELOCITY     = 2.5f;  //!< Return home at 2.5 m/s */
const static float LANDING_VELOCITY = 1.5f;  //!< Land at 1.5 m/s */

const static float DT               = 0.05f; // TODO: make the self monitored

//! Events that can be be injected into the FSM and trigger state changes
enum vtol_fsm_event {
	FSM_EVENT_AUTO,           /*!< Fake event to auto transition to the next state */
	FSM_EVENT_TIMEOUT,        /*!< The timeout configured expired */
	FSM_EVENT_HIT_TARGET,     /*!< The UAV hit the current target */
	FSM_EVENT_LEFT_TARGET,    /*!< The UAV left the target */
	FSM_EVENT_NUM_EVENTS
};

/**
 * The states the FSM's can be in. The actual behavior of the states is ultimately
 * determined by the entry function when enabling the state and the static method
 * that is called while staying in that state. In most cases the specific state also
 * sets the nav mode and a default methods will farm it out to the appropriate
 * algorithm.
 */
enum vtol_fsm_state {
	FSM_STATE_FAULT,           /*!< Invalid state transition occurred */
	FSM_STATE_INIT,            /*!< Starting state, normally auto transitions */
	FSM_STATE_HOLDING,         /*!< Holding at current location */
	FSM_STATE_FLYING_PATH,     /*!< Flying a path to a destination */
	FSM_STATE_LANDING,         /*!< Landing at a destination */
	FSM_STATE_PRE_RTH_HOLD,    /*!< Short hold before returning to home */
	FSM_STATE_POST_RTH_HOLD,   /*!< Hold at home before initiating landing */
	FSM_STATE_DISARM,          /*!< Disarm the system after landing */
	FSM_STATE_UNCHANGED,       /*!< Fake state to indicate do nothing */
	FSM_STATE_NUM_STATES
};

//! Structure for the FSM
struct vtol_fsm_transition {
	void (*entry_fn)();       /*!< Called when entering a state (i.e. activating a state) */
	int32_t (*static_fn)();   /*!< Called while in a state to update nav and check termination */
	enum vtol_fsm_state next_state[FSM_EVENT_NUM_EVENTS];
};

/**
 * Navigation modes that the states can enable. There is no one to one correspondence
 * between states and these navigation modes as some FSM might have multiple hold states
 * for example. When entering a hold state the FSM will configure the hold parameters and
 * then set the navgiation mode
 */
enum vtol_nav_mode {
	VTOL_NAV_HOLD,   /*!< Hold at the configured location @ref do_land*/
	VTOL_NAV_PATH,   /*!< Fly the configured path @ref do_path*/
	VTOL_NAV_LAND,   /*!< Land at the desired location @ref do_land */
	VTOL_NAV_IDLE    /*!< Nothing, no mode configured */
};

// State transition methods, typically enabling for certain actions
static void go_enable_hold_here();
static void go_enable_pause_10s_here();
static void go_enable_pause_home_10s();
static void go_enable_fly_home();
static void go_enable_land_home();

// Utility functions
static void configure_timeout(int32_t s);

// Methods that actually achieve the desired nav mode
static int32_t do_default();
static int32_t do_hold();
static int32_t do_path();
static int32_t do_land();

/**
 * The state machine for landing at home does the following:
 * 1. enable holding at the current location
 * 2  TODO: if it leaves the hold region enable a nav mode
 */
const static struct vtol_fsm_transition fsm_hold_position[FSM_STATE_NUM_STATES] = {
	[FSM_STATE_INIT] = {
		.next_state = {
			[FSM_EVENT_AUTO] = FSM_STATE_HOLDING,
		},
	},
	[FSM_STATE_HOLDING] = {
		.entry_fn = go_enable_hold_here,
		.next_state = {
			[FSM_EVENT_HIT_TARGET] = FSM_STATE_UNCHANGED,
			[FSM_EVENT_LEFT_TARGET] = FSM_STATE_UNCHANGED,
		},
	},
};

/**
 * The state machine for landing at home does the following:
 * 1. holds where currently at for 10 seconds
 * 2. flies to home at 2 m/s at either current altitude or 15 m above home
 * 3. holds above home for 10 seconds
 * 4. descends to ground
 * 5. disarms the system
 */
const static struct vtol_fsm_transition fsm_land_home[FSM_STATE_NUM_STATES] = {
	[FSM_STATE_INIT] = {
		.next_state = {
			[FSM_EVENT_AUTO] = FSM_STATE_PRE_RTH_HOLD,
		},
	},
	[FSM_STATE_PRE_RTH_HOLD] = {
		.entry_fn = go_enable_pause_10s_here,
		.next_state = {
			[FSM_EVENT_TIMEOUT] = FSM_STATE_FLYING_PATH,
			[FSM_EVENT_HIT_TARGET] = FSM_STATE_UNCHANGED,
			[FSM_EVENT_LEFT_TARGET] = FSM_STATE_UNCHANGED,
		},
	},
	[FSM_STATE_FLYING_PATH] = {
		.entry_fn = go_enable_fly_home,
		.next_state = {
			[FSM_EVENT_HIT_TARGET] = FSM_STATE_POST_RTH_HOLD,
		},
	},
	[FSM_STATE_POST_RTH_HOLD] = {
		.entry_fn = go_enable_pause_home_10s,
		.next_state = {
			[FSM_EVENT_TIMEOUT] = FSM_STATE_LANDING,
			[FSM_EVENT_HIT_TARGET] = FSM_STATE_UNCHANGED,
			[FSM_EVENT_LEFT_TARGET] = FSM_STATE_UNCHANGED,
		},
	},
	[FSM_STATE_LANDING] = {
		.entry_fn = go_enable_land_home,
		.next_state = {
			[FSM_EVENT_HIT_TARGET] = FSM_STATE_DISARM,
		},
	},

};

//! Tracks how many times the fsm_static has been called
static uint32_t current_count, set_time_count, timer_duration = 0;

void configure_timeout(int32_t s)
{
	set_time_count = current_count;
	timer_duration = s;
}

/**
 * @addtogroup VtolNavigationFsmMethods
 * These functions actually compute the control values to achieve the desired
 * navigation action.
 * @{
 */

//! The currently selected goal FSM
const static struct vtol_fsm_transition *current_goal;
//! The current state within the goal fsm
static enum vtol_fsm_state curr_state;

/**
 * Process any sequence of automatic state transitions
 */
static void vtol_fsm_process_auto()
{
	while (current_goal[curr_state].next_state[FSM_EVENT_AUTO]) {
		curr_state = current_goal[curr_state].next_state[FSM_EVENT_AUTO];

		/* Call the entry function (if any) for the next state. */
		if (current_goal[curr_state].entry_fn) {
			current_goal[curr_state].entry_fn();
		}
	}
}

/**
 * Initialize the selected FSM
 * @param goal The FSM to make active and initialize
 */
static void vtol_fsm_fsm_init(const struct vtol_fsm_transition *goal)
{
	current_goal = goal;
	curr_state = FSM_STATE_INIT;

	/* Call the entry function (if any) for the next state. */
	if (current_goal[curr_state].entry_fn) {
		current_goal[curr_state].entry_fn();
	}

	/* Process any AUTO transitions in the FSM */
	vtol_fsm_process_auto();
}

/**
 * Process an event in the currently active goal fsm
 *
 * This method will first update the current state @ref curr_state based on the
 * current state and the active @ref current_goal. When it enters a new state it
 * calls the appropriate entry_fn if it exists.
 */
static void vtol_fsm_inject_event(enum vtol_fsm_event event)
{
	// No need for mutexes here since this is called in a single threaded manner

	/*
	 * The STATE_UNCHANGED indicates to ignore these events
	 */
	if (current_goal[curr_state].next_state[event] == FSM_STATE_UNCHANGED)
		return;

	/*
	 * Move to the next state
	 *
	 * This is done prior to calling the new state's entry function to
	 * guarantee that the entry function never depends on the previous
	 * state.  This way, it cannot ever know what the previous state was.
	 */
	curr_state = current_goal[curr_state].next_state[event];

	/* Call the entry function (if any) for the next state. */
	if (current_goal[curr_state].entry_fn) {
		current_goal[curr_state].entry_fn();
	}

	/* Process any AUTO transitions in the FSM */
	vtol_fsm_process_auto();
}


/**
 * vtol_fsm_static is called regularly and checks whether a timeout event has occurred
 * and also runs the static method on the current state.
 *
 * @return 0 if successful or < 0 if an error occurred
 */
static int32_t vtol_fsm_static()
{
	VtolPathFollowerStatusFSM_StateSet((uint8_t *) &curr_state);

	// If the current state has a static function, call it
	if (current_goal[curr_state].static_fn)
		current_goal[curr_state].static_fn();
	else {
		int32_t ret_val;
		if ((ret_val = do_default()) < 0)
			return ret_val;
	}

	current_count++;

	if ((timer_duration > 0) && ((current_count - set_time_count) * DT) > timer_duration) {
		vtol_fsm_inject_event(FSM_EVENT_TIMEOUT);
	}

	return 0;
}

//! @}

/**
 * @addtogroup VtolNavigationDo
 * These functions actually compute the control values to achieve the desired
 * navigation action.
 * @{
 */

//! The currently configured navigation mode. Used to sanity check configuration.
static enum vtol_nav_mode vtol_nav_mode;

/**
 * General methods which based on the selected @ref vtol_nav_mode calls the appropriate
 * specific method
 */
static int32_t do_default()
{
	switch(vtol_nav_mode) {
	case VTOL_NAV_HOLD:
		return do_hold();
	case VTOL_NAV_PATH:
		return do_path();
	case VTOL_NAV_LAND:
		return do_land();
		break;
	default:
		// TODO: error?
		break;
	}

	return -1;
}

//! The setpoint for position hold relative to home in m
static float vtol_hold_position_ned[3];

/**
 * Update control values to stay at selected hold location.
 *
 * This method uses the vtol follower library to calculate the control values. The 
 * desired location is stored in @ref vtol_hold_position_ned.
 *
 * @return 0 if successful, <0 if failure
 */
static int32_t do_hold()
{
	if (vtol_follower_control_endpoint(DT, vtol_hold_position_ned) == 0) {
		if (vtol_follower_control_attitude(DT) == 0) {
			return 0;
		}
	}

	return -1;
}

//! The configured path desired. Uses the @ref PathDesired structure
static PathDesiredData vtol_fsm_path_desired;

/**
 * Update control values to fly along a path.
 *
 * This method uses the vtol follower library to calculate the control values. The 
 * desired path is stored in @ref vtol_fsm_path_desired.
 *
 * @return 0 if successful, <0 if failure
 */
static int32_t do_path()
{
	struct path_status progress;
	if (vtol_follower_control_path(DT, &vtol_fsm_path_desired, &progress) == 0) {
		if (vtol_follower_control_attitude(DT) == 0) {

			if (progress.fractional_progress >= 1.0f) {
				vtol_fsm_inject_event(FSM_EVENT_HIT_TARGET);
			}

			return 0;
		}
	}

	return -1;
}

/**
 * Update control values to land at @ref vtol_hold_position_ned.
 *
 * This method uses the vtol follower library to calculate the control values. The 
 * desired landing location is stored in @ref vtol_hold_position_ned.
 *
 * @return 0 if successful, <0 if failure
 */
static int32_t do_land()
{
	bool landed;
	if (vtol_follower_control_land(DT, vtol_hold_position_ned, LANDING_VELOCITY, &landed) == 0) {
		if (vtol_follower_control_attitude(DT) == 0) {
			return 0;
		}
	}

	return 0;
}

//! @}

/**
 * @addtogroup VtolNavigationEnable
 * Enable various actions. This configures the settings appropriately so that
 * the @ref VtolNavitgationDo methods can behave appropriately.
 * @{
 */

/**
 * Enable holding position at current location. Configures for hold.
 */
static void go_enable_hold_here()
{
	vtol_nav_mode = VTOL_NAV_HOLD;

	PositionActualData positionActual;
	PositionActualGet(&positionActual);

	vtol_hold_position_ned[0] = positionActual.North;
	vtol_hold_position_ned[1] = positionActual.East;
	vtol_hold_position_ned[2] = positionActual.Down;

	// Make sure we return at a minimum of 15 m above home
	if (vtol_hold_position_ned[2] > -RTH_MIN_ALTITUDE)
		vtol_hold_position_ned[2] = -RTH_MIN_ALTITUDE;

	configure_timeout(0);
}

/**
 * Enable holding position at current location for 10 s. Configures for hold.
 */
static void go_enable_pause_10s_here()
{
	vtol_nav_mode = VTOL_NAV_HOLD;

	PositionActualData positionActual;
	PositionActualGet(&positionActual);

	vtol_hold_position_ned[0] = positionActual.North;
	vtol_hold_position_ned[1] = positionActual.East;
	vtol_hold_position_ned[2] = positionActual.Down;

	// Make sure we return at a minimum of 15 m above home
	if (vtol_hold_position_ned[2] > -RTH_MIN_ALTITUDE)
		vtol_hold_position_ned[2] = -RTH_MIN_ALTITUDE;

	configure_timeout(10);
}

/**
 * Enable holding at home location for 10 s at current altitude. Configures for hold.
 */
static void go_enable_pause_home_10s()
{
	vtol_nav_mode = VTOL_NAV_HOLD;
	vtol_hold_position_ned[0] = 0;
	vtol_hold_position_ned[1] = 0;

	// This should already be >= from when RTH was initiated
	if (vtol_hold_position_ned[2] > -RTH_MIN_ALTITUDE)
		vtol_hold_position_ned[2] = -RTH_MIN_ALTITUDE;

	configure_timeout(10);
}

/**
 * Plot a course to home. Configures for path.
 */
static void go_enable_fly_home()
{
	vtol_nav_mode = VTOL_NAV_PATH;

	PositionActualData positionActual;
	PositionActualGet(&positionActual);

	// Set start position at current position
	vtol_fsm_path_desired.Start[0] = positionActual.North;
	vtol_fsm_path_desired.Start[1] = positionActual.East;
	vtol_fsm_path_desired.Start[2] = positionActual.Down;

	// Set end position above home
	vtol_fsm_path_desired.End[0] = 0;
	vtol_fsm_path_desired.End[1] = 0;
	vtol_fsm_path_desired.End[2] = positionActual.Down;
	if (vtol_fsm_path_desired.End[2] > -RTH_MIN_ALTITUDE)
		vtol_fsm_path_desired.End[2] = -RTH_MIN_ALTITUDE;

	vtol_fsm_path_desired.StartingVelocity = RTH_VELOCITY;
	vtol_fsm_path_desired.EndingVelocity = RTH_VELOCITY;

	vtol_fsm_path_desired.Mode = PATHDESIRED_MODE_FLYVECTOR;
	vtol_fsm_path_desired.ModeParameters = 0;

	PathDesiredSet(&vtol_fsm_path_desired);

	configure_timeout(0);
}

/**
 * Enable holding at home location for 10 s at current altitude. Configures for hold.
 */
static void go_enable_land_home()
{
	vtol_nav_mode = VTOL_NAV_LAND;

	vtol_hold_position_ned[0] = 0;
	vtol_hold_position_ned[1] = 0;
	vtol_hold_position_ned[2] = 0; // Has no affect

	configure_timeout(0);
}

//! @}

// Public API methods
int32_t vtol_follower_fsm_activate_goal(enum vtol_goals new_goal)
{
	switch(new_goal) {
	case GOAL_LAND_HOME:
		vtol_fsm_fsm_init(fsm_land_home);
		return 0;
	case GOAL_HOLD_POSITION:
		vtol_fsm_fsm_init(fsm_hold_position);
		return 0;
	default:
		return -1;
	}
}

int32_t vtol_follower_fsm_update()
{
	vtol_fsm_static();
	return 0;
}

//! @}