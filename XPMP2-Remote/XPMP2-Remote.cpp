/// @file       XPMP2-Remote.cpp
/// @brief      XPMP2 Remote Client: Displays aircraft served from other XPMP2-based plugins in the network
/// @details    This plugin is intended to be used in a multi-computer simulator
///             setup, usually in the PCs used for external visuals.\n
///             The typical setup would be:
///             - There is a multi-computer setup of one X-Plane Master PC,
///               which also runs one or more XPMP2-based plugins like LiveTraffic,
///               which create additional traffic, let's call them "traffic master".
///             - Other PCs serve to compute additional external visuals.
///               For them to be able to show the very same additional traffic
///               they run XPMP2 Remote Client, which will display a copy
///               of the traffic generated by the XPMP2-based plugin on the master.
///
///             Technically, this works as follows:
///             - The "traffic masters" will first _listen_
///               on the network if anyone is interested in their data.
///             - The XPMP2 Remote Client will first send a "beacon of interest"
///               message to the network.
///             - This messages tells the master plugins to start feeding their data.
///             - All communication is UDP multicast on the same multicast
///               group that X-Plane uses, too: 239.255.1.1, but on a different
///               port: 49788
///             - This generic way allows for many different setups:
///               - While the above might be typical, it is not of interest if the
///                 "traffic master" is running on the X-Plane Master or any
///                 other X-Plane instance in the network, for example to
///                 better balance the load.
///               - It could even be an X-Plane PC not included in the
///                 External Visuals setup, like for example in a Networked
///                 Multiplayer setup.
///               - Multiple XPMP2-based traffic masters can be active, and
///                 they can even run on different PCs. The one XPMP2 Remote Client
///                 per PC will still collect all traffic.
///               - If several traffic masters run on different PCs, then _all_
///                 PCs, including the ones running one of the traffic masters,
///                 will need to run the XPMP2 Remote Client, so that they
///                 pick up the traffic generated on the _other_ traffic masters.
///
/// @see        For multi-computer setup of external visual:
///             https://x-plane.com/manuals/desktop/#networkingmultiplecomputersformultipledisplays
/// @see        For Networked Multiplayer:
///             https://x-plane.com/manuals/desktop/#networkedmultiplayer
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
/// @copyright  Permission is hereby granted, free of charge, to any person obtaining a
///             copy of this software and associated documentation files (the "Software"),
///             to deal in the Software without restriction, including without limitation
///             the rights to use, copy, modify, merge, publish, distribute, sublicense,
///             and/or sell copies of the Software, and to permit persons to whom the
///             Software is furnished to do so, subject to the following conditions:\n
///             The above copyright notice and this permission notice shall be included in
///             all copies or substantial portions of the Software.\n
///             THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
///             IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
///             FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
///             AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
///             LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
///             OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
///             THE SOFTWARE.

// All headers are collected in one
#include "XPMP2-Remote.h"

//
// MARK: Utility Functions
//

/// This is a callback the XPMP2 calls regularly to learn about configuration settings.
/// Only 3 are left, all of them integers.
int CBIntPrefsFunc (const char *, [[maybe_unused]] const char * item, int defaultVal)
{
    // We always want to replace dataRefs and textures upon load to make the most out of the .obj files
    if (!strcmp(item, XPMP_CFG_ITM_REPLDATAREFS))   return glob.bObjReplDataRefs;       // taken from sending plugins
    if (!strcmp(item, XPMP_CFG_ITM_REPLTEXTURE))    return glob.bObjReplTextures;       // taken from sending plugins
    if (!strcmp(item, XPMP_CFG_ITM_CLAMPALL))       return 0;                           // Never needed: The defining coordinates are sent to us, don't interpret them here in any way
    if (!strcmp(item, XPMP_CFG_ITM_HANDLE_DUP_ID))  return 1;                           // must be on: if receiving from different plugins we can easily run in duplicate ids, which shall be handled
    if (!strcmp(item, XPMP_CFG_ITM_SUPPORT_REMOTE)) return -1;                          // We don't want this plugin to ever _send_ traffic!
    if (!strcmp(item, XPMP_CFG_ITM_LOGLEVEL))       return glob.logLvl;                 // taken from sending plugins
    if (!strcmp(item, XPMP_CFG_ITM_MODELMATCHING))  return glob.bLogMdlMatch;           // taken from sending plugins
    // Otherwise we just accept defaults
    return defaultVal;
}

//
// MARK: Menu functionality
//

/// menu id of our plugin's menu
XPLMMenuID hMenu = nullptr;

// menu indexes, also serving as menu item references
constexpr std::uintptr_t MENU_ACTIVE = 0;
constexpr std::uintptr_t MENU_TCAS   = 1;

/// Sets all menu checkmarks according to current status
void MenuUpdateCheckmarks ()
{
    // Menu item "Active"
    switch (XPMP2::RemoteGetStatus()) {
        case XPMP2::REMOTE_RECV_WAITING:
            XPLMSetMenuItemName(hMenu, MENU_ACTIVE, "Active (waiting for data)", 0);
            XPLMCheckMenuItem(hMenu, MENU_ACTIVE, xplm_Menu_Checked);
            break;

        case XPMP2::REMOTE_RECEIVING:
            XPLMSetMenuItemName(hMenu, MENU_ACTIVE, "Active", 0);
            XPLMCheckMenuItem(hMenu, MENU_ACTIVE, xplm_Menu_Checked);
            break;
            
        default:
            XPLMSetMenuItemName(hMenu, MENU_ACTIVE, "Activate (currently inactive)", 0);
            XPLMCheckMenuItem(hMenu, MENU_ACTIVE, xplm_Menu_Unchecked);
            break;
    }
    
    // Menu item "TCAS Control" (status display only, hence inactive)
    XPLMEnableMenuItem(hMenu, MENU_TCAS, false);
    XPLMCheckMenuItem(hMenu, MENU_TCAS, XPMPHasControlOfAIAircraft() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}



/// Callback function for menu
void MenuCallback (void* /*inMenuRef*/, void* inItemRef)
{
    // TODO: Need regular flight loop callback to get this updated
    
    switch (reinterpret_cast<std::uintptr_t>(inItemRef))
    {
        case MENU_ACTIVE:
            ClientToggleActive();
            break;
            
        case MENU_TCAS:
            break;
    }
    
    // Update check marks...things might have changed
    MenuUpdateCheckmarks();
}

//
// MARK: Standard Plugin Callbacks
//

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
#ifdef DEBUG
    glob.logLvl = logDEBUG;
#endif
    LOG_MSG(logMSG, "%s %.2f starting up...", REMOTE_CLIENT_NAME, REMOTE_CLIENT_VER);

    std::strcpy(outName, REMOTE_CLIENT_NAME);
	std::strcpy(outSig,  "TwinFan.plugin.XPMP2.Remote");
	std::strcpy(outDesc, "Remote Client displaying traffic generated by XPMP2-based plugins on the network");
    
    // use native paths, i.e. Posix style (as opposed to HFS style)
    // https://developer.x-plane.com/2014/12/mac-plugin-developers-you-should-be-using-native-paths/
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1);

    // Create the menu for the plugin
    int my_slot = XPLMAppendMenuItem(XPLMFindPluginsMenu(), REMOTE_CLIENT_NAME, NULL, 0);
    hMenu = XPLMCreateMenu(REMOTE_CLIENT_NAME, XPLMFindPluginsMenu(), my_slot, MenuCallback, NULL);
    XPLMAppendMenuItem(hMenu, "Active",             (void*)MENU_ACTIVE, 0);
    XPLMAppendMenuItem(hMenu, "TCAS Control",       (void*)MENU_TCAS, 0);
    MenuUpdateCheckmarks();
	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
}

PLUGIN_API int XPluginEnable(void)
{
    // The path separation character, one out of /\:
    char pathSep = XPLMGetDirectorySeparator()[0];
    // The plugin's path, results in something like ".../Resources/plugins/XPMP2-Remote/64/XPMP2-Remote.xpl"
    char szPath[256];
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, szPath, nullptr, nullptr);
    *(std::strrchr(szPath, pathSep)) = 0;   // Cut off the plugin's file name
    *(std::strrchr(szPath, pathSep)+1) = 0; // Cut off the "64" directory name, but leave the dir separation character
    // We search in a subdirectory named "Resources" for all we need
    std::string resourcePath = szPath;
    resourcePath += "Resources";            // should now be something like ".../Resources/plugins/XPMP2-Sample/Resources"

    // Try initializing XPMP2:
    const char *res = XPMPMultiplayerInit (REMOTE_CLIENT_NAME,      // plugin name,
                                           resourcePath.c_str(),    // path to supplemental files
                                           CBIntPrefsFunc,          // configuration callback function
                                           "A320",                  // default ICAO type
                                           REMOTE_CLIENT_SHORT);    // plugin short name
    if (res[0]) {
        LOG_MSG(logFATAL, "Initialization of XPMP2 failed: %s", res);
        return 0;
    }
    
    // Load our CSL models
    res = XPMPLoadCSLPackage(resourcePath.c_str());     // CSL folder root path
    if (res[0]) {
        LOG_MSG(logERR, "Error while loading CSL packages: %s", res);
    }
    
    // Activate the listener
    ClientToggleActive();
    MenuUpdateCheckmarks();
    
    // Success
    LOG_MSG(logINFO, "Enabled");
	return 1;
}

PLUGIN_API void XPluginDisable(void)
{
    // Give up AI plane control
    XPMPMultiplayerDisable();
        
    // Properly cleanup the XPMP2 library
    XPMPMultiplayerCleanup();
    
    LOG_MSG(logINFO, "Disabled");
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID who, long inMsg, void*)
{
    // Some other plugin wants TCAS/AI control, but we don't release as we are displaying live aicraft
    if (inMsg == XPLM_MSG_RELEASE_PLANES)
        LOG_MSG(logINFO, "%s requested TCAS access", GetPluginName(who).c_str())
}
