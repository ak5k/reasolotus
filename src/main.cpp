#include "reaper_vararg.hpp"
#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#define EXTNAME "ReaSolotus"

int solotus_command_id {-1};
int solotus_state {0};
std::mutex m {};
std::atomic_bool atomic_bool_lock {false};

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

// void RemoveSend(MediaTrack* src, MediaTrack* dst)
// {
//     for (int i = 0; i < GetTrackNumSends(src, 0); i++) {
//         auto p = (MediaTrack*)(uintptr_t)
//             GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
//         if (p == dst) {
//             RemoveTrackSend(src, 0, i);
//             break;
//         }
//     }
// }

// void CreateSend(MediaTrack* src, MediaTrack* dst)
// {
//     bool found {false};
//     for (int i = 0; i < GetTrackNumSends(src, 0); i++) {
//         auto p = (MediaTrack*)(uintptr_t)
//             GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
//         if (p == dst) {
//             found = true;
//             break;
//         }
//     }
//     if (!found) {
//         auto n = CreateTrackSend(src, dst);
//         SetTrackSendInfo_Value(src, 0, n, "I_SENDMODE", 3);
//     }
// }

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

    return res;
}

void Organize()
{
    auto master = GetMasterTrack(0);
    auto mixbus = GetMixbus();
    auto solobus = GetSolobus();
    for (auto i = 0; i < GetNumTracks(); i++) {
        auto tr = GetTrack(0, i);
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
            if (!HasSend(tr, parent)) {
                CreateTrackSend(tr, parent);
            }
            for (int j = 0; j < GetTrackNumSends(tr, 0); j++) {
                auto dst = (MediaTrack*)(uintptr_t)
                    GetTrackSendInfo_Value(tr, 0, j, "P_DESTTRACK");
                if (dst == mixbus) {
                    RemoveTrackSend(tr, 0, j);
                }
            }
        }
        if (tr != solobus) {
            SetMediaTrackInfo_Value(tr, "B_MAINSEND", 0);
        }
    }
}

void DoSolo(std::set<MediaTrack*>& queue)
{
    PreventUIRefresh(1);
    Undo_BeginBlock2(0);
    Organize();
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
        if (!queue.contains(src)) {
            SetTrackSendInfo_Value(solobus, -1, i, "B_MUTE", 1);
        }
    }

    for (int i = 0; i < GetTrackNumSends(mixbus, 0); i++) {
        auto dst = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(mixbus, 0, i, "P_DESTTRACK");
        if (dst == solobus) {
            SetTrackSendInfo_Value(
                mixbus,
                0,
                i,
                "B_MUTE",
                AnyTrackSolo(0) ? 1 : 0);
        }
    }

    Undo_EndBlock2(0, "ReaSolotus", 0);
    PreventUIRefresh(-1);
}

void DoMute(MediaTrack* tr, bool mute)
{
    PreventUIRefresh(1);
    Undo_BeginBlock2(0);
    auto solobus = GetSolobus();
    for (int i = 0; i < GetTrackNumSends(tr, 0); i++) {
        auto dst = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(tr, 0, i, "P_DESTTRACK");
        if (dst != solobus) {
            SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", mute ? 1 : 0);
        }
    }

    Undo_EndBlock2(0, "ReaSolotus: Mute", 0);
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

        std::set<MediaTrack*> queue {};
        for (int i = 0; i < GetNumTracks(); i++) {
            auto tr = GetTrack(0, i);
            int state {};
            (void)GetTrackState(tr, &state);
            if (state & 16) {
                queue.insert(tr);
            }
        }

        atomic_bool_lock = true;
        DoSolo(queue);
        atomic_bool_lock = false;
    }

    void SetSurfaceMute(MediaTrack* trackid, bool mute)
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