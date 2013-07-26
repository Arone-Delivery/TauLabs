/**
 ******************************************************************************
 *
 * @file       vehicleconfigurationhelper.cpp
 * @brief      Provide an interface between the settings selected and the wizard
 *             and storing them on the FC
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2012.
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2013
 * @see        The GNU Public License (GPL) Version 3
 *
 * @addtogroup GCSPlugins GCS Plugins
 * @{
 * @addtogroup NavWizard Setup Wizard
 * @{
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

#include "vehicleconfigurationhelper.h"
#include "extensionsystem/pluginmanager.h"
#include "actuatorsettings.h"
#include "attitudesettings.h"
#include "mixersettings.h"
#include "systemsettings.h"
#include "manualcontrolsettings.h"
#include "sensorsettings.h"
#include "stabilizationsettings.h"

VehicleConfigurationHelper::VehicleConfigurationHelper(VehicleConfigurationSource *configSource)
    : m_configSource(configSource), m_uavoManager(0),
    m_transactionOK(false), m_transactionTimeout(false), m_currentTransactionObjectID(-1),
    m_progress(0)
{
    Q_ASSERT(m_configSource);
    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    m_uavoManager = pm->getObject<UAVObjectManager>();
    Q_ASSERT(m_uavoManager);
}

bool VehicleConfigurationHelper::setupVehicle(bool save)
{
    m_progress = 0;
    clearModifiedObjects();
    resetVehicleConfig();
    resetGUIData();
    if (!saveChangesToController(save)) {
        return false;
    }

    m_progress = 0;
    applyModuleConfiguration();
    applyFilterConfiguration();

    bool result = saveChangesToController(save);
    emit saveProgress(m_modifiedObjects.count() + 1, ++m_progress, result ? tr("Done!") : tr("Failed!"));
    return result;
}

void VehicleConfigurationHelper::addModifiedObject(UAVDataObject *object, QString description)
{
    m_modifiedObjects << new QPair<UAVDataObject *, QString>(object, description);
}

void VehicleConfigurationHelper::clearModifiedObjects()
{
    for (int i = 0; i < m_modifiedObjects.count(); i++) {
        QPair<UAVDataObject *, QString> *pair = m_modifiedObjects.at(i);
        delete pair;
    }
    m_modifiedObjects.clear();
}

/**
 * @brief VehicleConfigurationHelper::applyFilterConfiguration Apply
 * settings for the attitude estimation filter
 *
 * The settings to apply were determined previously during the wizard.
 */
void VehicleConfigurationHelper::applyFilterConfiguration()
{
    /*Core::IBoardType* boardPlugin = m_configSource->getControllerType();
    Q_ASSERT(boardPlugin);
    if (!boardPlugin)
        return;

    Core::IBoardType::InputType newType = m_configSource->getInputType();
    bool success = boardPlugin->setInputOnPort(newType);

    if (success) {
        UAVDataObject* hwSettings = dynamic_cast<UAVDataObject*>(
                    m_uavoManager->getObject(boardPlugin->getHwUAVO()));
        Q_ASSERT(hwSettings);
        if (hwSettings)
            addModifiedObject(hwSettings, tr("Writing hardware settings"));
    } */
}

void VehicleConfigurationHelper::applyModuleConfiguration()
{

}

bool VehicleConfigurationHelper::saveChangesToController(bool save)
{
    qDebug() << "Saving modified objects to controller. " << m_modifiedObjects.count() << " objects in found.";
    const int OUTER_TIMEOUT = 3000 * 20; // 10 seconds timeout for saving all objects
    const int INNER_TIMEOUT = 2000; // 1 second timeout on every save attempt

    ExtensionSystem::PluginManager *pm = ExtensionSystem::PluginManager::instance();
    Q_ASSERT(pm);
    UAVObjectUtilManager *utilMngr     = pm->getObject<UAVObjectUtilManager>();
    Q_ASSERT(utilMngr);

    QTimer outerTimeoutTimer;
    outerTimeoutTimer.setSingleShot(true);

    QTimer innerTimeoutTimer;
    innerTimeoutTimer.setSingleShot(true);

    connect(utilMngr, SIGNAL(saveCompleted(int, bool)), this, SLOT(uAVOTransactionCompleted(int, bool)));
    connect(&innerTimeoutTimer, SIGNAL(timeout()), &m_eventLoop, SLOT(quit()));
    connect(&outerTimeoutTimer, SIGNAL(timeout()), this, SLOT(saveChangesTimeout()));

    outerTimeoutTimer.start(OUTER_TIMEOUT);
    for (int i = 0; i < m_modifiedObjects.count(); i++) {
        QPair<UAVDataObject *, QString> *objPair = m_modifiedObjects.at(i);
        m_transactionOK = false;
        UAVDataObject *obj     = objPair->first;
        QString objDescription = objPair->second;
        if (UAVObject::GetGcsAccess(obj->getMetadata()) != UAVObject::ACCESS_READONLY && obj->isSettings()) {
            emit saveProgress(m_modifiedObjects.count() + 1, ++m_progress, objDescription);

            m_currentTransactionObjectID = obj->getObjID();

            connect(obj, SIGNAL(transactionCompleted(UAVObject *, bool)), this, SLOT(uAVOTransactionCompleted(UAVObject *, bool)));
            while (!m_transactionOK && !m_transactionTimeout) {
                // Allow the transaction to take some time
                innerTimeoutTimer.start(INNER_TIMEOUT);

                // Set object updated
                obj->updated();
                if (!m_transactionOK) {
                    m_eventLoop.exec();
                }
                innerTimeoutTimer.stop();
            }
            disconnect(obj, SIGNAL(transactionCompleted(UAVObject *, bool)), this, SLOT(uAVOTransactionCompleted(UAVObject *, bool)));
            if (m_transactionOK) {
                qDebug() << "Object " << obj->getName() << " was successfully updated.";
                if (save) {
                    m_transactionOK = false;
                    m_currentTransactionObjectID = obj->getObjID();
                    // Try to save until success or timeout
                    while (!m_transactionOK && !m_transactionTimeout) {
                        // Allow the transaction to take some time
                        innerTimeoutTimer.start(INNER_TIMEOUT);

                        // Persist object in controller
                        utilMngr->saveObjectToFlash(obj);
                        if (!m_transactionOK) {
                            m_eventLoop.exec();
                        }
                        innerTimeoutTimer.stop();
                    }
                    m_currentTransactionObjectID = -1;
                }
            }

            if (!m_transactionOK) {
                qDebug() << "Transaction timed out when trying to save: " << obj->getName();
            } else {
                qDebug() << "Object " << obj->getName() << " was successfully saved.";
            }
        } else {
            qDebug() << "Trying to save a UAVDataObject that is read only or is not a settings object.";
        }
        if (m_transactionTimeout) {
            qDebug() << "Transaction timed out when trying to save " << m_modifiedObjects.count() << " objects.";
            break;
        }
    }

    outerTimeoutTimer.stop();
    disconnect(&outerTimeoutTimer, SIGNAL(timeout()), this, SLOT(saveChangesTimeout()));
    disconnect(&innerTimeoutTimer, SIGNAL(timeout()), &m_eventLoop, SLOT(quit()));
    disconnect(utilMngr, SIGNAL(saveCompleted(int, bool)), this, SLOT(uAVOTransactionCompleted(int, bool)));

    qDebug() << "Finished saving modified objects to controller. Success = " << m_transactionOK;

    return m_transactionOK;
}

void VehicleConfigurationHelper::uAVOTransactionCompleted(int oid, bool success)
{
    if (oid == m_currentTransactionObjectID) {
        m_transactionOK = success;
        m_eventLoop.quit();
    }
}

void VehicleConfigurationHelper::uAVOTransactionCompleted(UAVObject *object, bool success)
{
    if (object) {
        uAVOTransactionCompleted(object->getObjID(), success);
    }
}

void VehicleConfigurationHelper::saveChangesTimeout()
{
    m_transactionOK = false;
    m_transactionTimeout = true;
    m_eventLoop.quit();
}

void VehicleConfigurationHelper::resetVehicleConfig()
{
/*    // Reset all vehicle data
    MixerSettings *mSettings = MixerSettings::GetInstance(m_uavoManager);

    // Reset feed forward, accel times etc
    mSettings->setFeedForward(0.0f);
    mSettings->setMaxAccel(1000.0f);
    mSettings->setAccelTime(0.0f);
    mSettings->setDecelTime(0.0f);

    // Reset throttle curves
    QString throttlePattern = "ThrottleCurve%1";
    for (int i = 1; i <= 2; i++) {
        UAVObjectField *field = mSettings->getField(throttlePattern.arg(i));
        Q_ASSERT(field);
        for (quint32 i = 0; i < field->getNumElements(); i++) {
            field->setValue(i * (1.0f / (field->getNumElements() - 1)), i);
        }
    }

    // Reset Mixer types and values
    QString mixerTypePattern   = "Mixer%1Type";
    QString mixerVectorPattern = "Mixer%1Vector";
    for (int i = 1; i <= 10; i++) {
        UAVObjectField *field = mSettings->getField(mixerTypePattern.arg(i));
        Q_ASSERT(field);
        field->setValue(field->getOptions().at(0));

        field = mSettings->getField(mixerVectorPattern.arg(i));
        Q_ASSERT(field);
        for (quint32 i = 0; i < field->getNumElements(); i++) {
            field->setValue(0, i);
        }
    }

    // Apply updates
    // mSettings->setData(mSettings->getData());
    addModifiedObject(mSettings, tr("Preparing mixer settings")); */
}

void VehicleConfigurationHelper::resetGUIData()
{
    SystemSettings *sSettings = SystemSettings::GetInstance(m_uavoManager);

    Q_ASSERT(sSettings);
    SystemSettings::DataFields data = sSettings->getData();
    data.AirframeType = SystemSettings::AIRFRAMETYPE_CUSTOM;
    for (quint32 i = 0; i < SystemSettings::AIRFRAMECATEGORYSPECIFICCONFIGURATION_NUMELEM; i++) {
        data.AirframeCategorySpecificConfiguration[i] = 0;
    }
    sSettings->setData(data);
    addModifiedObject(sSettings, tr("Preparing vehicle settings"));
}

/**
 * @}
 * @}
 */