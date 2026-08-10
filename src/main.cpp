// #include "reaper_vararg.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#define REAPERAPI_IMPLEMENT

#include <WDL/wdltypes.h>

#include <reaper_plugin_functions.h>

#include "configvar.h"

#define EXTNAME "ReaSolotus"

namespace reasolotus
{
void CreateSoloBus(MediaTrack*& res, GUID*& g, char buf[512]);

void CreateMixBus(MediaTrack*& res, GUID*& g, char buf[512]);

void Unregister();

struct ReaSolotusState
{
    ReaSolotusState()
    {
        projectStateText.reserve(BUFSIZ);
        projectStateBuffer.resize(BUFSIZ);
    }

    int commandId{-1};
    int initCommandId{-1};
    int initMultichannelCommandId{-1};
    int enabled{0};
    bool multichannel{false};
    std::atomic_bool callbackGuard{false};
    std::mutex mutex{};
    std::set<MediaTrack*> soloQueue{};
    std::unordered_map<MediaTrack*, std::vector<MediaTrack*>> mutedSends{};
    std::string projectStateText{};
    std::vector<char> projectStateBuffer{};
};

ReaSolotusState state{};

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
    if (!strcmp(varName, "vzoom2"))
    { // set both vzoom3 and vzoom2 (below)
        ConfigVar<float>("vzoom3").try_set(static_cast<float>(newValue));
    }
    if (ConfigVar<int>(varName).try_set(newValue))
        return true;
    if (ConfigVar<char> cv{varName})
    {
        if (newValue > std::numeric_limits<char>::max() || newValue < std::numeric_limits<char>::min())
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
    auto* master = GetMasterTrack(0);
    if (dst == master && (bool)GetMediaTrackInfo_Value(src, "B_MAINSEND") && !GetParentTrack(src))
        return true;
    for (int i = 0; i < GetTrackNumSends(src, 0); i++)
    {
        auto* p = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
        if (p == dst)
        {
            res = true;
            break;
        }
    }
    return res;
}

MediaTrack* GetMixbus()
{
    static MediaTrack* res = nullptr;

    if (res != nullptr && ValidatePtr2(0, res, "MediaTrack*"))
        return res;

    res = nullptr;

    char buf[BUFSIZ]{};
    GUID* g{};
    GetProjExtState(0, EXTNAME, "Mix", buf, BUFSIZ);

    std::string s = buf;

    for (int i = 0; i < GetNumTracks(); i++)
    {
        auto* tr = GetTrack(0, i);
        g = GetTrackGUID(tr);
        guidToString(g, buf);
        if (s == buf)
            res = tr;
    }

    // CreateMixBus(res, g, buf);

    return res;
}

void CreateMixBus(MediaTrack* res)
{
    if (res == nullptr)
    {
        InsertTrackAtIndex(GetNumTracks(), false);
        res = GetTrack(0, GetNumTracks() - 1);
        GetSetMediaTrackInfo_String(res, "P_NAME", (char*)"Mix", true);
        SetMediaTrackInfo_Value(res, "B_MAINSEND", 0);
        SetMediaTrackInfo_Value(res, "B_SOLO_DEFEAT", 1);

        SetOnlyTrackSelected(res);
        ReorderSelectedTracks(0, 0);
        SetTrackSelected(res, false);
    }
    GUID* g;
    g = GetTrackGUID(res);
    char buf[512];
    guidToString(g, buf);
    SetProjExtState(0, EXTNAME, "Mix", buf);
}

MediaTrack* GetSolobus()
{
    static MediaTrack* res = nullptr;

    if (res != nullptr && ValidatePtr2(0, res, "MediaTrack*"))
        return res;

    res = nullptr;

    char buf[BUFSIZ]{};
    GUID* g{};
    GetProjExtState(0, EXTNAME, "Solo", buf, BUFSIZ);

    std::string s = buf;

    for (int i = 0; i < GetNumTracks(); i++)
    {
        auto* tr = GetTrack(0, i);
        g = GetTrackGUID(tr);
        guidToString(g, buf);
        if (s == buf)
            res = tr;
    }

    // CreateSoloBus(res, g, buf);

    return res;
}

void CreateSoloBus(MediaTrack* res)
{
    if (res == nullptr)
    {
        InsertTrackAtIndex(GetNumTracks(), false);
        res = GetTrack(0, GetNumTracks() - 1);
        GetSetMediaTrackInfo_String(res, "P_NAME", (char*)"Solo", true);
        SetMediaTrackInfo_Value(res, "B_SOLO_DEFEAT", 1);

        SetOnlyTrackSelected(res);
        ReorderSelectedTracks(0, 0);
        SetTrackSelected(res, false);
    }

    GUID* g;
    g = GetTrackGUID(res);
    char buf[512];
    guidToString(g, buf);
    SetProjExtState(0, EXTNAME, "Solo", buf);
}

void ConfigureSendChannels(int trackSendId, MediaTrack* sendTrack, MediaTrack* receiveTrack)
{
    if (!state.multichannel)
        return;

    int sendNumChannels = (int)GetMediaTrackInfo_Value(sendTrack, "I_NCHAN");
    int receiveNumChannels = (int)GetMediaTrackInfo_Value(receiveTrack, "I_NCHAN");

    if (sendNumChannels > receiveNumChannels)
        GetSetMediaTrackInfo(receiveTrack, "I_NCHAN", &sendNumChannels);

    if (sendNumChannels > 2)
    {
        int chanFlag = (sendNumChannels << 9);
        SetTrackSendInfo_Value(sendTrack, 0, trackSendId, "I_SRCCHAN", chanFlag);
        SetTrackSendInfo_Value(sendTrack, 0, trackSendId, "I_DSTCHAN", 0);
    }
}

int CreateManagedSend(MediaTrack* sendTrack, MediaTrack* receiveTrack)
{
    int trackSendId = CreateTrackSend(sendTrack, receiveTrack);
    ConfigureSendChannels(trackSendId, sendTrack, receiveTrack);
    return trackSendId;
}

void Organize()
{
    auto* master = GetMasterTrack(0);
    auto* mixbus = GetMixbus();
    auto* solobus = GetSolobus();
    auto folderFound{false};
    // auto hasSends {false};

    if (!mixbus || !solobus)
        return;

    for (auto i = 0; i < GetNumTracks(); i++)
    {
        auto* tr = GetTrack(0, i);

        if (GetTrackNumSends(tr, 1) > 0)
            SetMediaTrackInfo_Value(tr, "B_SOLO_DEFEAT", 1);

        if (HasSend(tr, master) && tr != mixbus && tr != solobus && tr != master)
            CreateManagedSend(tr, mixbus);

        if (!HasSend(tr, solobus) && tr != solobus && tr != master)
        {
            auto j = CreateManagedSend(tr, solobus);
            SetTrackSendInfo_Value(tr, 0, j, "B_MUTE", (tr == mixbus) ? 0 : 1);
            SetTrackSendInfo_Value(tr, 0, j, "I_SENDMODE", 3);
        }

        auto* parent = GetParentTrack(tr);

        if (parent != nullptr)
        {
            folderFound = true;
            auto hasParentSend = GetMediaTrackInfo_Value(tr, "B_MAINSEND");
            if (!HasSend(tr, parent) && hasParentSend > 0)
                CreateManagedSend(tr, parent);
            for (int j = 0; j < GetTrackNumSends(tr, 0); j++)
            {
                auto* dst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(tr, 0, j, "P_DESTTRACK");
                if (dst == mixbus)
                    RemoveTrackSend(tr, 0, j);
            }
            auto parentIdx = (int)GetMediaTrackInfo_Value(parent, "IP_TRACKNUMBER");

            SetOnlyTrackSelected(tr);
            ReorderSelectedTracks(parentIdx - 1, 0);
            SetTrackSelected(tr, false);
        }

        if (tr != solobus)
            SetMediaTrackInfo_Value(tr, "B_MAINSEND", 0);

        // if (GetTrackNumSends(tr, 0) > 2) {
        //     hasSends = true;
        // }
    }

    SetMediaTrackInfo_Value(mixbus, "B_SOLO_DEFEAT", 1);
    SetMediaTrackInfo_Value(solobus, "B_SOLO_DEFEAT", 1);

    if (folderFound)
        ShowConsoleMsg("ReaSolotus: Folders not supported. Use sends instead. \n");

    char buf[BUFSIZ];
    if (get_config_var_string("soloip", buf, BUFSIZ) && std::stoi(buf) != 0)
        SNM_SetIntConfigVar("soloip", 0);
    ;
}

void DoSolo()
{
    auto* solobus = GetSolobus();
    auto* mixbus = GetMixbus();

    if (!solobus || !mixbus)
    {
        ShowConsoleMsg("ReaSolotus: Initialize project \n");
        return;
    }

    PreventUIRefresh(1);
    // Undo_BeginBlock2(0);
    if (!state.soloQueue.empty())
        Organize();

    bool isMixbusSoloed = state.soloQueue.contains(mixbus);

    for (auto* tr : state.soloQueue)
    {
        for (int i = 0; i < GetTrackNumSends(tr, 0); i++)
        {
            auto dst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(tr, 0, i, "P_DESTTRACK");
            if (dst == solobus)
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 0);
        }
    }

    for (int i = 0; i < GetTrackNumSends(solobus, -1); i++)
    {
        auto* src = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(solobus, -1, i, "P_SRCTRACK");
        if (!state.soloQueue.contains(src) && src != mixbus)
        {
            auto isMuted = (bool)GetTrackSendInfo_Value(solobus, -1, i, "B_MUTE");
            if (!isMuted)
                SetTrackSendInfo_Value(solobus, -1, i, "B_MUTE", 1);
        }
    }

    for (int i = 0; i < GetTrackNumSends(mixbus, 0); i++)
    {
        auto* dst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(mixbus, 0, i, "P_DESTTRACK");
        if (dst == solobus)
        {
            auto isMuted = (bool)GetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE");
            if (isMuted && !AnyTrackSolo(0))
                SetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE", 0);
            else if (!isMuted && AnyTrackSolo(0) && !isMixbusSoloed)
                SetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE", 1);
        }
    }
    // Undo_EndBlock2(0, "ReaSolotus", 0);
    PreventUIRefresh(-1);
}

template <typename T>
void removeFirst(std::vector<T>& vec, const T& element)
{
    auto it = std::find(vec.begin(), vec.end(), element);
    if (it != vec.end())
        vec.erase(it);
}

template <typename T>
bool hasElement(const std::vector<T>& vec, const T& element)
{
    return std::find(vec.begin(), vec.end(), element) != vec.end();
}

void AppendGuidToProjectState(GUID* guid)
{
    char buf[BUFSIZ];
    guidToString(guid, buf);
    state.projectStateText += buf;
}

MediaTrack* FindTrackByGuid(const char* guidText, size_t guidLength)
{
    for (int i = 0; i < GetNumTracks(); i++)
    {
        auto* tr = GetTrack(0, i);
        auto* g = GetTrackGUID(tr);
        char buf[BUFSIZ];
        guidToString(g, buf);
        if (strlen(buf) == guidLength && !strncmp(buf, guidText, guidLength))
            return tr;
    }
    return nullptr;
}

void SerializeMutedSends()
{
    state.projectStateText.clear();
    for (const auto& pair : state.mutedSends)
    {
        AppendGuidToProjectState(GetTrackGUID(pair.first));
        state.projectStateText.push_back(':');
        for (auto* element : pair.second)
        {
            AppendGuidToProjectState(GetTrackGUID(element));
            state.projectStateText.push_back(',');
        }
        state.projectStateText.push_back(';');
    }
}

void DeserializeMutedSends(const std::string& str)
{
    state.mutedSends.clear();

    size_t entryStart = 0;
    while (entryStart < str.size())
    {
        size_t entryEnd = str.find(';', entryStart);
        if (entryEnd == std::string::npos)
            entryEnd = str.size();

        if (entryEnd == entryStart)
        {
            entryStart = entryEnd + 1;
            continue;
        }

        size_t keyEnd = str.find(':', entryStart);
        if (keyEnd == std::string::npos || keyEnd > entryEnd)
        {
            entryStart = entryEnd + 1;
            continue;
        }

        MediaTrack* key = FindTrackByGuid(str.c_str() + entryStart, keyEnd - entryStart);
        if (key == nullptr)
        {
            entryStart = entryEnd + 1;
            continue;
        }

        auto& value = state.mutedSends[key];
        size_t valueStart = keyEnd + 1;
        while (valueStart < entryEnd)
        {
            size_t valueEnd = str.find(',', valueStart);
            if (valueEnd == std::string::npos || valueEnd > entryEnd)
                valueEnd = entryEnd;

            MediaTrack* track = FindTrackByGuid(str.c_str() + valueStart, valueEnd - valueStart);
            if (track != nullptr)
                value.push_back(track);

            valueStart = valueEnd + 1;
        }

        entryStart = entryEnd + 1;
    }
}

void StoreStringToProjExtState(const std::string& key, const std::string& str)
{
    state.projectStateText = str;
    state.projectStateText.push_back('#');
    SetProjExtState(0, EXTNAME, key.c_str(), state.projectStateText.c_str());
    state.projectStateText.pop_back();
}

const std::string& LoadStringFromProjExtState(const std::string& key)
{
    int bufferSize = (int)state.projectStateBuffer.size();
    GetProjExtState(0, EXTNAME, key.c_str(), state.projectStateBuffer.data(), bufferSize);
    state.projectStateText = state.projectStateBuffer.data();

    while (!state.projectStateText.empty() && state.projectStateText.find('#') == std::string::npos)
    {
        state.projectStateBuffer.resize(state.projectStateBuffer.size() * 2);
        bufferSize = (int)state.projectStateBuffer.size();
        GetProjExtState(0, EXTNAME, key.c_str(), state.projectStateBuffer.data(), bufferSize);
        state.projectStateText = state.projectStateBuffer.data();
    }

    size_t endPos = state.projectStateText.find('#');
    if (endPos != std::string::npos)
        state.projectStateText.resize(endPos);

    return state.projectStateText;
}

void DoMute(MediaTrack* tr, bool mute)
{
    // auto* mixbus = GetMixbus();
    auto* solobus = GetSolobus();
    if (!solobus)
    {
        ShowConsoleMsg("ReaSolotus: Initialize project \n");
        return;
    }

    PreventUIRefresh(1);
    DeserializeMutedSends(LoadStringFromProjExtState("mutedSends"));

    for (int i = 0; i < GetTrackNumSends(tr, 0); i++)
    {
        auto* dst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(tr, 0, i, "P_DESTTRACK");
        if (dst != solobus)
        {
            auto isMuted = (bool)GetTrackSendInfo_Value(tr, 0, i, "B_MUTE");
            if (!isMuted && mute)
            {
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 1);
                state.mutedSends[tr].push_back(dst);
            }
            else if (isMuted && !mute && hasElement(state.mutedSends[tr], dst))
            {
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 0);
                removeFirst(state.mutedSends[tr], dst);
            }
        }
    }
    SerializeMutedSends();
    StoreStringToProjExtState("mutedSends", state.projectStateText);
    PreventUIRefresh(-1);
}

void ShutDown();

void Initialize()
{
    auto* mixbus = GetMixbus();
    auto* solobus = GetSolobus();
    if (!mixbus)
        CreateMixBus(mixbus);
    if (!solobus)
        CreateSoloBus(solobus);
    Organize();
    ShowConsoleMsg("ReaSolotus: Remember to set output for Mix bus. Master is the solo/control room/phones output.\n");
}

static bool
CommandHook(KbdSectionInfo* sec, const int command, const int val, const int valhw, const int relmode, HWND hwnd)
{
    (void)sec;
    (void)val;
    (void)valhw;
    (void)relmode;
    (void)hwnd;

    if (command == state.initCommandId)
    {
        state.multichannel = false;
        Initialize();
        return true;
    }
    else if (command == state.initMultichannelCommandId)
    {
        state.multichannel = true;
        Initialize();
        return true;
    }

    std::scoped_lock lock(state.mutex);
    if (command == state.commandId)
    {
        state.enabled = !state.enabled;
        if (!state.enabled)
            ShutDown();
        return true;
    }

    return false;
}

int ToggleActionCallback(int command_id)
{
    std::scoped_lock lock(state.mutex);
    if (command_id == state.commandId)
        return state.enabled;

    return -1;
}

custom_action_register_t action_registration{0, "AK5K_REASOLOTUS", "ReaSolotus", nullptr};
custom_action_register_t action_init_registration{0, "AK5K_REASOLOTUS_INIT", "ReaSolotus Init", nullptr};
custom_action_register_t action_init_multichannel_registration{
    0,
    "AK5K_REASOLOTUS_INIT_MULTICHANNEL",
    "ReaSolotus Init (MultiChannel Support)",
    nullptr
};

void Register()
{
    state.commandId = plugin_register("custom_action", &action_registration);
    state.initCommandId = plugin_register("custom_action", &action_init_registration);
    state.initMultichannelCommandId = plugin_register("custom_action", &action_init_multichannel_registration);

    plugin_register("hookcommand2", (void*)&CommandHook);
    plugin_register("toggleaction", (void*)&ToggleActionCallback);
}

void Unregister()
{
    plugin_register("-toggleaction", (void*)&ToggleActionCallback);
    plugin_register("-hookcommand2", (void*)&CommandHook);

    if (state.commandId > 0)
        plugin_register("-custom_action", &action_registration);
    if (state.initCommandId > 0)
        plugin_register("-custom_action", &action_init_registration);
    if (state.initMultichannelCommandId > 0)
        plugin_register("-custom_action", &action_init_multichannel_registration);
}

class ReaSolotus : public IReaperControlSurface
{
public:
    const char* GetTypeString() override
    {
        return "REASOLOTUS";
    }

    const char* GetDescString() override
    {
        return "ReaSolotus";
    }

    const char* GetConfigString() override
    {
        return "";
    }

    void SetSurfaceSolo(MediaTrack* trackid, bool solo) override
    {
        // to avoid internal recursion
        if (state.callbackGuard)
            return;

        std::scoped_lock lock(state.mutex);
        if (state.enabled == 0)
            return;
        auto* master = GetMasterTrack(0);
        if (trackid != master)
            return;

        state.callbackGuard = true;
        state.soloQueue.clear();
        if (solo)
        {
            for (int i = 0; i < GetNumTracks(); i++)
            {
                auto* tr = GetTrack(0, i);
                int state{};
                (void)GetTrackState(tr, &state);
                if (state & 16)
                    reasolotus::state.soloQueue.insert(tr);
            }
        }

        DoSolo();
        state.callbackGuard = false;
    }

    void SetSurfaceMute(MediaTrack* trackid, bool mute) override
    {
        // to avoid internal recursion
        if (state.callbackGuard)
            return;

        std::scoped_lock lock(state.mutex);
        if (state.enabled == 0)
            return;
        auto* master = GetMasterTrack(0);
        if (trackid == master)
            return;

        state.callbackGuard = true;
        DoMute(trackid, mute);
        state.callbackGuard = false;
    }
};

void ShutDown()
{
    auto* solobus = GetSolobus();
    for (int i = 0; i < GetNumTracks(); i++)
    {
        auto* tr = GetTrack(0, i);
        DoMute(tr, false);
        for (int i = 0; i < GetTrackNumSends(tr, 0); i++)
        {
            auto* dst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(tr, 0, i, "P_DESTTRACK");
            if (dst == solobus)
                SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 1);
        }
    }
}

} // namespace reasolotus

reasolotus::ReaSolotus* csurf{};

extern "C"
{
    REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec)
    {
        (void)hInstance;
        if (!rec)
        {
            if (csurf)
            {
                plugin_register("-csurf_inst", csurf);
                reasolotus::Unregister();
                delete csurf;
                csurf = nullptr;
            }
            return 0;
        }

        csurf = new reasolotus::ReaSolotus();
        if (rec->caller_version != REAPER_PLUGIN_VERSION || REAPERAPI_LoadAPI(rec->GetFunc))
        {
            delete csurf;
            csurf = nullptr;
            return 0;
        }

        reasolotus::Register();
        plugin_register("csurf_inst", csurf);
        return 1;
    }
}
