#include "reaper_vararg.hpp"
#include <mutex>
#include <string>
#include <vector>

#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#define EXTNAME "ReaSolotus"

int solotus_command_id {-1};
int solotus_state {0};
std::mutex m {};

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

void RemoveSend(MediaTrack* src, MediaTrack* dst)
{
    for (int i = 0; i < GetTrackNumSends(src, 0); i++) {
        auto p = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
        if (p == dst) {
            RemoveTrackSend(src, 0, i);
            break;
        }
    }
}

void CreateSend(MediaTrack* src, MediaTrack* dst)
{
    bool found {false};
    for (int i = 0; i < GetTrackNumSends(src, 0); i++) {
        auto p = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
        if (p == dst) {
            found = true;
            break;
        }
    }
    if (!found) {
        CreateTrackSend(src, dst);
    }
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
    GetProjExtState(0, EXTNAME, "Mixbus", buf, BUFSIZ);

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
        GetSetMediaTrackInfo_String(res, "P_NAME", (char*)"Mixbus", true);
        SetMediaTrackInfo_Value(res, "B_MAINSEND", 0);
        SetMediaTrackInfo_Value(res, "B_SOLO_DEFEAT", 1);
    }

    g = GetTrackGUID(res);
    guidToString(g, buf);
    SetProjExtState(0, EXTNAME, "Mixbus", buf);

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
    GetProjExtState(0, EXTNAME, "Solotus", buf, BUFSIZ);

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
        GetSetMediaTrackInfo_String(res, "P_NAME", (char*)"Solotus", true);
        SetMediaTrackInfo_Value(res, "B_SOLO_DEFEAT", 1);
    }

    g = GetTrackGUID(res);
    guidToString(g, buf);
    SetProjExtState(0, EXTNAME, "Solotus", buf);

    return res;
}

void Organize()
{
    auto master = GetMasterTrack(0);
    auto mixbus = GetMixbus();
    auto solobus = GetSolobus();

    for (int i = 0; i < GetNumTracks(); i++) {
        auto tr = GetTrack(0, i);
        if (HasSend(tr, master) && tr != mixbus && tr != solobus) {
            CreateSend(tr, mixbus);
            SetMediaTrackInfo_Value(tr, "B_MAINSEND", 0);
        }
    }
}

void DoSolo(MediaTrack* tr, bool solo)
{
    PreventUIRefresh(1);
    Undo_BeginBlock();
    Organize();
    auto mixbus = GetMixbus();
    auto solobus = GetSolobus();
    if (solo) {
        CreateSend(tr, solobus);
    }
    else {
        RemoveSend(tr, solobus);
    }

    if (AnyTrackSolo(0)) {
        SetMediaTrackInfo_Value(mixbus, "B_MAINSEND", 0);
    }
    else {
        SetMediaTrackInfo_Value(mixbus, "B_MAINSEND", 1);
    }

    Undo_EndBlock("ReaSolotus", 0);
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
        // std::scoped_lock lock(m);
        if (solotus_state == 0 || trackid == GetMasterTrack(0)) {
            return;
        }
        DoSolo(trackid, solo);
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