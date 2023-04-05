#include "reaper_vararg.hpp"
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#include "configvar.h"

#define EXTNAME "ReaSolotus"

int solotus_command_id {-1};
int solotus_state {0};
std::mutex m {};
std::atomic_bool atomic_bool_lock {false};

// from SWS/SNM extension
/******************************************************************************
/ SnM_Util.cpp
/
/ Copyright (c) 2012 and later Jeffos
/
/
/ Permission is hereby granted, free of charge, to any person obtaining a copy
/ of this software and associated documentation files (the "Software"), to deal
/ in the Software without restriction, including without limitation the rights
to / use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies / of the Software, and to permit persons to whom the Software is
furnished to / do so, subject to the following conditions:
/
/ The above copyright notice and this permission notice shall be included in all
/ copies or substantial portions of the Software.
/
/ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
/ EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
/ OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
/ NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
/ HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
/ WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/ FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
/ OTHER DEALINGS IN THE SOFTWARE.
/
******************************************************************************/
bool SNM_SetIntConfigVar(const char* varName, const int newValue)
{
    if (!strcmp(varName, "vzoom2")) // set both vzoom3 and vzoom2 (below)
        ConfigVar<float>("vzoom3").try_set(static_cast<float>(newValue));
    if (ConfigVar<int>(varName).try_set(newValue))
        return true;
    if (ConfigVar<char> cv {varName}) {
        if (newValue > std::numeric_limits<char>::max() ||
            newValue < std::numeric_limits<char>::min())
            return false;

        *cv = newValue;
        return true;
    }

    return false;
}
// ****************************************************************************
// end SnM_Util.cpp

bool HasSend(MediaTrack* src, MediaTrack* dst)
{
    bool res = false;
    auto master = GetMasterTrack(0);
    if (dst == master && (bool)GetMediaTrackInfo_Value(src, "B_MAINSEND") &&
        !GetParentTrack(src)) {
        return true;
    }
    for (int i = 0; i < GetTrackNumSends(src, 0); i++) {
        auto p = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
        if (p == dst) {
            res = true;
            break;
        }
    }
    return res;
}

MediaTrack* GetMixbus()
{
    static MediaTrack* res = nullptr;

    if (res != nullptr && ValidatePtr2(0, res, "MediaTrack*")) {
        return res;
    }
    else {
        res = nullptr;
    }

    char buf[BUFSIZ] {};
    GUID* g {};
    GetProjExtState(0, EXTNAME, "Mix", buf, BUFSIZ);

    std::string s = buf;

    for (int i = 0; i < GetNumTracks(); i++) {
        auto tr = GetTrack(0, i);
        g = GetTrackGUID(tr);
        guidToString(g, buf);
        if (s.compare(buf) == 0) {
            res = tr;
        }
    }

    if (res == nullptr) {
        InsertTrackAtIndex(GetNumTracks(), false);
        res = GetTrack(0, GetNumTracks() - 1);
        GetSetMediaTrackInfo_String(res, "P_NAME", (char*)"Mix", true);
        SetMediaTrackInfo_Value(res, "B_MAINSEND", 0);
        SetMediaTrackInfo_Value(res, "B_SOLO_DEFEAT", 1);
    }

    g = GetTrackGUID(res);
    guidToString(g, buf);
    SetProjExtState(0, EXTNAME, "Mix", buf);

    SetOnlyTrackSelected(res);
    ReorderSelectedTracks(0, 0);
    SetTrackSelected(res, false);

    return res;
}

MediaTrack* GetSolobus()
{
    static MediaTrack* res = nullptr;

    if (res != nullptr && ValidatePtr2(0, res, "MediaTrack*")) {
        return res;
    }
    else {
        res = nullptr;
    }

    char buf[BUFSIZ] {};
    GUID* g {};
    GetProjExtState(0, EXTNAME, "Solo", buf, BUFSIZ);

    std::string s = buf;

    for (int i = 0; i < GetNumTracks(); i++) {
        auto tr = GetTrack(0, i);
        g = GetTrackGUID(tr);
        guidToString(g, buf);
        if (s.compare(buf) == 0) {
            res = tr;
        }
    }

    if (res == nullptr) {
        InsertTrackAtIndex(GetNumTracks(), false);
        res = GetTrack(0, GetNumTracks() - 1);
        GetSetMediaTrackInfo_String(res, "P_NAME", (char*)"Solo", true);
        SetMediaTrackInfo_Value(res, "B_SOLO_DEFEAT", 1);
    }

    g = GetTrackGUID(res);
    guidToString(g, buf);
    SetProjExtState(0, EXTNAME, "Solo", buf);

    SetOnlyTrackSelected(res);
    ReorderSelectedTracks(0, 0);
    SetTrackSelected(res, false);

    return res;
}

void Organize()
{
    auto master = GetMasterTrack(0);
    auto mixbus = GetMixbus();
    auto solobus = GetSolobus();
    auto folderFound {false};
    auto hasSends {false};

    for (auto i = 0; i < GetNumTracks(); i++) {
        auto tr = GetTrack(0, i);

        if (GetTrackNumSends(tr, 1) > 0) {
            SetMediaTrackInfo_Value(tr, "B_SOLO_DEFEAT", 1);
        }

        if (HasSend(tr, master) && tr != mixbus && tr != solobus &&
            tr != master) {
            CreateTrackSend(tr, mixbus);
        }

        if (!HasSend(tr, solobus) && tr != solobus && tr != master) {
            auto j = CreateTrackSend(tr, solobus);
            SetTrackSendInfo_Value(tr, 0, j, "B_MUTE", (tr == mixbus) ? 0 : 1);
            SetTrackSendInfo_Value(tr, 0, j, "I_SENDMODE", 3);
        }

        auto parent = GetParentTrack(tr);

        if (parent != nullptr) {
            folderFound = true;
            auto hasParentSend = GetMediaTrackInfo_Value(tr, "B_MAINSEND");
            if (!HasSend(tr, parent) && hasParentSend > 0) {
                CreateTrackSend(tr, parent);
            }
            for (int j = 0; j < GetTrackNumSends(tr, 0); j++) {
                auto dst = (MediaTrack*)(uintptr_t)
                    GetTrackSendInfo_Value(tr, 0, j, "P_DESTTRACK");
                if (dst == mixbus) {
                    RemoveTrackSend(tr, 0, j);
                }
            }
            auto parentIdx =
                (int)GetMediaTrackInfo_Value(parent, "IP_TRACKNUMBER");

            SetOnlyTrackSelected(tr);
            ReorderSelectedTracks(parentIdx - 1, 0);
            SetTrackSelected(tr, false);
        }

        if (tr != solobus) {
            SetMediaTrackInfo_Value(tr, "B_MAINSEND", 0);
        }

        // if (GetTrackNumSends(tr, 0) > 2) {
        //     hasSends = true;
        // }
    }

    SetMediaTrackInfo_Value(mixbus, "B_SOLO_DEFEAT", 1);
    SetMediaTrackInfo_Value(solobus, "B_SOLO_DEFEAT", 1);

    if (folderFound) {
        ShowConsoleMsg(
            "ReaSolotus: Folders not supported. Use sends instead. \n");
    }

    char buf[BUFSIZ];
    if (get_config_var_string("soloip", buf, BUFSIZ) && std::stoi(buf) != 0) {
        SNM_SetIntConfigVar("soloip", 0);
    };
}

void DoSolo(std::set<MediaTrack*>& queue)
{
    PreventUIRefresh(1);
    // Undo_BeginBlock2(0);
    if (!queue.empty()) {
        Organize();
    }
    auto solobus = GetSolobus();
    auto mixbus = GetMixbus();
    for (auto&& tr : queue) {
        for (int i = 0; i < GetTrackNumSends(tr, 0); i++) {
            auto dst = (MediaTrack*)(uintptr_t)
                GetTrackSendInfo_Value(tr, 0, i, "P_DESTTRACK");
            if (dst == solobus) {
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 0);
            }
        }
    }

    for (int i = 0; i < GetTrackNumSends(solobus, -1); i++) {
        auto src = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(solobus, -1, i, "P_SRCTRACK");
        if (!queue.contains(src) && src != mixbus) {
            auto isMuted =
                (bool)GetTrackSendInfo_Value(solobus, -1, i, "B_MUTE");
            if (!isMuted) {
                SetTrackSendInfo_Value(solobus, -1, i, "B_MUTE", 1);
            }
        }
    }

    for (int i = 0; i < GetTrackNumSends(mixbus, 0); i++) {
        auto dst = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(mixbus, 0, i, "P_DESTTRACK");
        if (dst == solobus) {
            auto isMuted = (bool)GetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE");
            if (isMuted && !AnyTrackSolo(0)) {
                SetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE", 0);
            }
            else if (!isMuted && AnyTrackSolo(0)) {
                SetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE", 1);
            }
        }
    }
    // Undo_EndBlock2(0, "ReaSolotus", 0);
    PreventUIRefresh(-1);
}

void DoMute(MediaTrack* tr, bool mute)
{
    PreventUIRefresh(1);
    (void)GetMixbus();
    auto solobus = GetSolobus();
    for (int i = 0; i < GetTrackNumSends(tr, 0); i++) {
        auto dst = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(tr, 0, i, "P_DESTTRACK");
        if (dst != solobus) {
            auto isMuted = (bool)GetTrackSendInfo_Value(tr, 0, i, "B_MUTE");
            if (!isMuted && mute) {
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 1);
            }
            else if (isMuted && !mute) {
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 0);
            }
        }
    }
    PreventUIRefresh(-1);
}

static bool CommandHook(
    KbdSectionInfo* sec,
    const int command,
    const int val,
    const int valhw,
    const int relmode,
    HWND hwnd)
{
    std::scoped_lock lock(m);
    (void)sec;
    (void)val;
    (void)valhw;
    (void)relmode;
    (void)hwnd;

    if (command == solotus_command_id) {
        solotus_state = !solotus_state;
        return true;
    }

    return false;
}

int ToggleActionCallback(int command_id)
{
    std::scoped_lock lock(m);
    if (command_id == solotus_command_id) {
        return solotus_state;
    }

    return -1;
}

void Register()
{
    custom_action_register_t action {
        0,
        "AK5K_REASOLOTUS",
        "ReaSolotus",
        nullptr};

    solotus_command_id = plugin_register("custom_action", &action);

    plugin_register("hookcommand2", (void*)&CommandHook);
    plugin_register("toggleaction", (void*)&ToggleActionCallback);
}

class ReaSolotus : public IReaperControlSurface {
  public:
    const char* GetTypeString()
    {
        return "REASOLOTUS";
    }
    const char* GetDescString()
    {
        return "ReaSolotus";
    }
    const char* GetConfigString()
    {
        return "";
    }
    void SetSurfaceSolo(MediaTrack* trackid, bool solo)
    {
        // to avoid internal recursion
        if (atomic_bool_lock == true) {
            return;
        }

        std::scoped_lock lock(m);
        if (solotus_state == 0) {
            return;
        }
        auto master = GetMasterTrack(0);
        if (trackid != master) {
            return;
        }

        atomic_bool_lock = true;
        std::set<MediaTrack*> queue {};
        if (solo) {
            for (int i = 0; i < GetNumTracks(); i++) {
                auto tr = GetTrack(0, i);
                int state {};
                (void)GetTrackState(tr, &state);
                if (state & 16) {
                    queue.insert(tr);
                }
            }
        }

        DoSolo(queue);
        atomic_bool_lock = false;
    }

    void SetSurfaceMute(MediaTrack* trackid, bool mute)
    {
        // to avoid internal recursion
        if (atomic_bool_lock == true) {
            return;
        }

        // std::scoped_lock lock(m);
        if (solotus_state == 0) {
            return;
        }
        auto master = GetMasterTrack(0);
        if (trackid == master) {
            return;
        }

        atomic_bool_lock = true;
        DoMute(trackid, mute);
        atomic_bool_lock = false;
    }
};

ReaSolotus* csurf;

extern "C" {
REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
    REAPER_PLUGIN_HINSTANCE hInstance,
    reaper_plugin_info_t* rec)
{
    (void)hInstance;
    csurf = new ReaSolotus();
    if (!rec) {
        delete csurf;
        return 0;
    }
    else if (
        rec->caller_version != REAPER_PLUGIN_VERSION ||
        REAPERAPI_LoadAPI(rec->GetFunc)) {
        return 0;
    }
    Register();
    plugin_register("csurf_inst", csurf);
    return 1;
}
}