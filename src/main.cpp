#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
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
class ReaSolotus;

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
    int enabled{0};
    std::atomic_bool callbackGuard{false};
    std::mutex mutex{};
    std::set<MediaTrack*> soloQueue{};
    std::unordered_map<MediaTrack*, std::vector<MediaTrack*>> mutedSends{};
    std::string projectStateText{};
    std::vector<char> projectStateBuffer{};
    ReaSolotus* csurf{};
};

ReaSolotusState* state{};

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

MediaTrack* FindTrackByStoredGuid(const char* extStateKey)
{
    char storedGuid[BUFSIZ]{};
    GetProjExtState(0, EXTNAME, extStateKey, storedGuid, BUFSIZ);

    for (int i = 0; i < GetNumTracks(); i++)
    {
        MediaTrack* track = GetTrack(0, i);
        GUID* guid = GetTrackGUID(track);
        char trackGuid[BUFSIZ]{};
        guidToString(guid, trackGuid);
        if (std::string(storedGuid) == trackGuid)
            return track;
    }

    return nullptr;
}

MediaTrack* GetMixbus()
{
    return FindTrackByStoredGuid("Mix");
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

    GUID* guid = GetTrackGUID(res);
    char guidText[512];
    guidToString(guid, guidText);
    SetProjExtState(0, EXTNAME, "Mix", guidText);
}

MediaTrack* GetSolobus()
{
    return FindTrackByStoredGuid("Solo");
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

    GUID* guid = GetTrackGUID(res);
    char guidText[512];
    guidToString(guid, guidText);
    SetProjExtState(0, EXTNAME, "Solo", guidText);
}

int FindSendToTrack(MediaTrack* src, MediaTrack* dst)
{
    for (int i = 0; i < GetTrackNumSends(src, 0); i++)
    {
        MediaTrack* sendDst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(src, 0, i, "P_DESTTRACK");
        if (sendDst == dst)
            return i;
    }

    return -1;
}

int BuildSourceChannelSpec(int channelCount)
{
    int sourceChannelSpec = 0;

    if (channelCount <= 0)
        return sourceChannelSpec;

    if (channelCount == 1)
        sourceChannelSpec = 1;
    else if (channelCount > 2)
        sourceChannelSpec = channelCount / 2;

    return sourceChannelSpec;
}

void SanitizeSendToBus(MediaTrack* sendTrack, MediaTrack* receiveBus)
{
    if (sendTrack == nullptr || receiveBus == nullptr)
        return;

    int sendIdx = FindSendToTrack(sendTrack, receiveBus);
    if (sendIdx < 0)
        return;

    int trackChannels = (int)GetMediaTrackInfo_Value(sendTrack, "I_NCHAN");
    int busChannels = (int)GetMediaTrackInfo_Value(receiveBus, "I_NCHAN");
    int sendChannelCount = std::min(trackChannels, busChannels);

    if (sendChannelCount <= 0)
        return;

    if (sendChannelCount > 1 && (sendChannelCount % 2) != 0)
        sendChannelCount--;

    int sourceChannelSpec = BuildSourceChannelSpec(sendChannelCount);

    SetTrackSendInfo_Value(sendTrack, 0, sendIdx, "I_SRCCHAN", sourceChannelSpec << 10);
    SetTrackSendInfo_Value(sendTrack, 0, sendIdx, "I_DSTCHAN", 0);
}

void SanitizeSoloBusChannels(MediaTrack* mixbus, MediaTrack* solobus)
{
    if (mixbus == nullptr || solobus == nullptr)
        return;

    int mixbusChannels = (int)GetMediaTrackInfo_Value(mixbus, "I_NCHAN");
    int solobusChannels = (int)GetMediaTrackInfo_Value(solobus, "I_NCHAN");

    if (mixbusChannels > 0 && solobusChannels != mixbusChannels)
        SetMediaTrackInfo_Value(solobus, "I_NCHAN", mixbusChannels);
}

void ConfigureSendChannels(int trackSendId, MediaTrack* sendTrack)
{
    if (trackSendId < 0)
        return;

    int sendNumChannels = (int)GetMediaTrackInfo_Value(sendTrack, "I_NCHAN");

    int sourceChannelSpec = 0;
    if (sendNumChannels == 1)
        sourceChannelSpec = 1;
    else if (sendNumChannels > 2)
        sourceChannelSpec = sendNumChannels / 2;

    SetTrackSendInfo_Value(sendTrack, 0, trackSendId, "I_SRCCHAN", sourceChannelSpec << 10);
    SetTrackSendInfo_Value(sendTrack, 0, trackSendId, "I_DSTCHAN", 0);
}

int CreateManagedSend(MediaTrack* sendTrack, MediaTrack* receiveTrack)
{
    int trackSendId = CreateTrackSend(sendTrack, receiveTrack);
    ConfigureSendChannels(trackSendId, sendTrack);
    return trackSendId;
}

void Organize()
{
    MediaTrack* master = GetMasterTrack(0);
    MediaTrack* mixbus = GetMixbus();
    MediaTrack* solobus = GetSolobus();
    bool folderFound = false;
    int trackCount = GetNumTracks();

    if (!mixbus || !solobus)
        return;

    for (int i = 0; i < trackCount; i++)
    {
        MediaTrack* tr = GetTrack(0, i);

        if (GetTrackNumSends(tr, 1) > 0)
            SetMediaTrackInfo_Value(tr, "B_SOLO_DEFEAT", 1);

        if (HasSend(tr, master) && tr != mixbus && tr != solobus && tr != master)
            CreateManagedSend(tr, mixbus);

        if (!HasSend(tr, solobus) && tr != solobus && tr != master)
        {
            int j = CreateManagedSend(tr, solobus);
            SetTrackSendInfo_Value(tr, 0, j, "B_MUTE", (tr == mixbus) ? 0 : 1);
            SetTrackSendInfo_Value(tr, 0, j, "I_SENDMODE", 3);
        }

        MediaTrack* parent = GetParentTrack(tr);

        if (parent != nullptr)
        {
            folderFound = true;
            double hasParentSend = GetMediaTrackInfo_Value(tr, "B_MAINSEND");
            if (!HasSend(tr, parent) && hasParentSend > 0)
                CreateManagedSend(tr, parent);
            for (int j = 0; j < GetTrackNumSends(tr, 0); j++)
            {
                MediaTrack* dst = (MediaTrack*)(uintptr_t)GetTrackSendInfo_Value(tr, 0, j, "P_DESTTRACK");
                if (dst == mixbus)
                    RemoveTrackSend(tr, 0, j);
            }
            int parentIdx = (int)GetMediaTrackInfo_Value(parent, "IP_TRACKNUMBER");

            SetOnlyTrackSelected(tr);
            ReorderSelectedTracks(parentIdx - 1, 0);
            SetTrackSelected(tr, false);
        }

        if (tr != solobus)
            SetMediaTrackInfo_Value(tr, "B_MAINSEND", 0);
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

    SanitizeSoloBusChannels(mixbus, solobus);

    SanitizeSendToBus(mixbus, solobus);

    bool hasRelevantSolo = !state->soloQueue.empty();
    bool isMixbusSoloed = state->soloQueue.contains(mixbus);

    for (auto* tr : state->soloQueue)
    {
        SanitizeSendToBus(tr, solobus);
        SanitizeSendToBus(tr, mixbus);

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
        if (!state->soloQueue.contains(src) && src != mixbus)
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
            if (isMuted && !hasRelevantSolo)
                SetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE", 0);
            else if (!isMuted && hasRelevantSolo && !isMixbusSoloed)
                SetTrackSendInfo_Value(mixbus, 0, i, "B_MUTE", 1);
        }
    }
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
    state->projectStateText += buf;
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
    state->projectStateText.clear();
    for (const auto& pair : state->mutedSends)
    {
        AppendGuidToProjectState(GetTrackGUID(pair.first));
        state->projectStateText.push_back(':');
        for (auto* element : pair.second)
        {
            AppendGuidToProjectState(GetTrackGUID(element));
            state->projectStateText.push_back(',');
        }
        state->projectStateText.push_back(';');
    }
}

void DeserializeMutedSends(const std::string& str)
{
    state->mutedSends.clear();

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

        auto& value = state->mutedSends[key];
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
    state->projectStateText = str;
    state->projectStateText.push_back('#');
    SetProjExtState(0, EXTNAME, key.c_str(), state->projectStateText.c_str());
    state->projectStateText.pop_back();
}

const std::string& LoadStringFromProjExtState(const std::string& key)
{
    int bufferSize = (int)state->projectStateBuffer.size();
    GetProjExtState(0, EXTNAME, key.c_str(), state->projectStateBuffer.data(), bufferSize);
    state->projectStateText = state->projectStateBuffer.data();

    while (!state->projectStateText.empty() && state->projectStateText.find('#') == std::string::npos)
    {
        state->projectStateBuffer.resize(state->projectStateBuffer.size() * 2);
        bufferSize = (int)state->projectStateBuffer.size();
        GetProjExtState(0, EXTNAME, key.c_str(), state->projectStateBuffer.data(), bufferSize);
        state->projectStateText = state->projectStateBuffer.data();
    }

    size_t endPos = state->projectStateText.find('#');
    if (endPos != std::string::npos)
        state->projectStateText.resize(endPos);

    return state->projectStateText;
}

void DoMute(MediaTrack* tr, bool mute)
{
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
                state->mutedSends[tr].push_back(dst);
            }
            else if (isMuted && !mute)
            {
                auto mutedSendIt = state->mutedSends.find(tr);
                if (mutedSendIt != state->mutedSends.end() && hasElement(mutedSendIt->second, dst))
                {
                    SetTrackSendInfo_Value(tr, 0, i, "B_MUTE", 0);
                    removeFirst(mutedSendIt->second, dst);
                    if (mutedSendIt->second.empty())
                        state->mutedSends.erase(mutedSendIt);
                }
            }
        }
    }
    SerializeMutedSends();
    StoreStringToProjExtState("mutedSends", state->projectStateText);
    PreventUIRefresh(-1);
}

void ShutDown();

void Initialize()
{
    Undo_BeginBlock();

    MediaTrack* mixbus = GetMixbus();
    MediaTrack* solobus = GetSolobus();
    if (!mixbus)
        CreateMixBus(mixbus);
    if (!solobus)
        CreateSoloBus(solobus);

    mixbus = GetMixbus();
    solobus = GetSolobus();

    Organize();

    Undo_EndBlock("ReaSolotus Init", -1);

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

    if (!state)
        return false;

    if (command == state->initCommandId)
    {
        Initialize();
        return true;
    }

    std::scoped_lock lock(state->mutex);
    if (command == state->commandId)
    {
        state->enabled = !state->enabled;
        if (!state->enabled)
            ShutDown();
        return true;
    }

    return false;
}

int ToggleActionCallback(int command_id)
{
    if (!state)
        return -1;

    std::scoped_lock lock(state->mutex);
    if (command_id == state->commandId)
        return state->enabled;

    return -1;
}

custom_action_register_t action_registration{0, "AK5K_REASOLOTUS", "ReaSolotus", nullptr};
custom_action_register_t action_init_registration{0, "AK5K_REASOLOTUS_INIT", "ReaSolotus Init", nullptr};

void Register()
{
    if (!state)
        state = new ReaSolotusState();

    state->commandId = plugin_register("custom_action", &action_registration);
    state->initCommandId = plugin_register("custom_action", &action_init_registration);

    plugin_register("hookcommand2", (void*)&CommandHook);
    plugin_register("toggleaction", (void*)&ToggleActionCallback);
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
        if (!state || state->callbackGuard)
            return;

        std::scoped_lock lock(state->mutex);
        if (state->enabled == 0)
            return;
        auto* master = GetMasterTrack(0);
        auto* solobus = GetSolobus();
        if (trackid != master)
            return;

        state->callbackGuard = true;
        state->soloQueue.clear();
        if (solo)
        {
            for (int i = 0; i < GetNumTracks(); i++)
            {
                auto* tr = GetTrack(0, i);
                int trackState{};
                (void)GetTrackState(tr, &trackState);
                if ((trackState & 16) && tr != solobus)
                    state->soloQueue.insert(tr);
            }
        }

        DoSolo();
        state->callbackGuard = false;
    }

    void SetSurfaceMute(MediaTrack* trackid, bool mute) override
    {
        // to avoid internal recursion
        if (!state || state->callbackGuard)
            return;

        std::scoped_lock lock(state->mutex);
        if (state->enabled == 0)
            return;
        auto* master = GetMasterTrack(0);
        if (trackid == master)
            return;

        state->callbackGuard = true;
        DoMute(trackid, mute);
        state->callbackGuard = false;
    }
};

void Unregister()
{
    if (!state)
        return;

    plugin_register("-toggleaction", (void*)&ToggleActionCallback);
    plugin_register("-hookcommand2", (void*)&CommandHook);

    if (state->commandId > 0)
        plugin_register("-custom_action", &action_registration);
    if (state->initCommandId > 0)
        plugin_register("-custom_action", &action_init_registration);

    delete state->csurf;
    state->csurf = nullptr;

    delete state;
    state = nullptr;
}

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

extern "C"
{
    REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec)
    {
        (void)hInstance;
        if (!rec)
        {
            if (reasolotus::state && reasolotus::state->csurf)
            {
                plugin_register("-csurf_inst", reasolotus::state->csurf);
                reasolotus::Unregister();
            }
            return 0;
        }

        if (rec->caller_version != REAPER_PLUGIN_VERSION || REAPERAPI_LoadAPI(rec->GetFunc))
            return 0;

        reasolotus::Register();
        reasolotus::state->csurf = new reasolotus::ReaSolotus();
        plugin_register("csurf_inst", reasolotus::state->csurf);
        return 1;
    }
}
