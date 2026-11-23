/*
 * Proton-Scarlett — C++20 Header Compatibility Check for winrt_ui ABI
 */
#include "../winrt_ui_abi.h"

extern "C" int uwp_winrt_ui_cxx_abi_check(void) {
    return (int)sizeof(UwpIDisplayInformationVtbl) +
           (int)sizeof(UwpIApplicationViewVtbl) +
           (int)sizeof(UwpIUIViewSettingsVtbl) +
           (int)sizeof(UwpISystemNavigationManagerVtbl) +
           (int)sizeof(UwpIPointerVisualizationSettingsVtbl) +
           (int)sizeof(UwpIPointerVisualizationSettingsStaticsVtbl) +
           (int)sizeof(UwpIApiInformationStaticsVtbl) +
           (int)sizeof(UwpIHdmiDisplayInformationVtbl);
}
