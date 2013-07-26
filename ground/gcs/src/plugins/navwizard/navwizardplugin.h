/**
 ******************************************************************************
 *
 * @file       navwizardplugin.h
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
#ifndef NAVWIZARDPLUGIN_H
#define NAVWIZARDPLUGIN_H

#include <extensionsystem/iplugin.h>
#include <QWizard>
#include "setupwizard.h"
#include "navigationwizard.h"

class NavWizardPlugin : public ExtensionSystem::IPlugin {
    Q_OBJECT
public:
    NavWizardPlugin();
    ~NavWizardPlugin();

    void extensionsInitialized();
    bool initialize(const QStringList & arguments, QString *errorString);
    void shutdown();

private slots:
    void showNavigationWizard();
    void wizardTerminated();

private:
    bool wizardRunning;
};

#endif // NAVWIZARDPLUGIN_H
