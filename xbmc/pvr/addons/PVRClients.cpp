/*
 *      Copyright (C) 2005-2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "PVRClients.h"

#include "Application.h"
#include "settings/GUISettings.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogSelect.h"
#include "pvr/PVRManager.h"
#include "pvr/PVRDatabase.h"
#include "guilib/GUIWindowManager.h"
#include "settings/Settings.h"
#include "pvr/channels/PVRChannelGroups.h"
#include "pvr/recordings/PVRRecordings.h"
#include "pvr/timers/PVRTimers.h"
#include "utils/StringUtils.h"

#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif

using namespace std;
using namespace ADDON;
using namespace PVR;
using namespace EPG;

CPVRClients::CPVRClients(void) :
    CThread("PVR add-on updater"),
    m_bChannelScanRunning(false),
    m_bIsSwitchingChannels(false),
    m_bIsValidChannelSettings(false),
    m_bIsPlayingLiveTV(false),
    m_bIsPlayingRecording(false),
    m_scanStart(0)
{
}

CPVRClients::~CPVRClients(void)
{
  Unload();
}

void CPVRClients::Start(void)
{
  Stop();

  Create();
  SetPriority(-1);
}

void CPVRClients::Stop(void)
{
  StopThread();
}

bool CPVRClients::IsConnectedClient(int iClientId)
{
  PVR_CLIENT client;
  return GetConnectedClient(iClientId, client);
}

int CPVRClients::GetClientId(const AddonPtr client) const
{
  CSingleLock lock(m_critSection);

  for (PVR_CLIENTMAP_CITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
    if (itr->second->ID() == client->ID())
      return itr->first;

  return -1;
}

bool CPVRClients::GetConnectedClient(int iClientId, PVR_CLIENT &addon) const
{
  bool bReturn(false);
  if (iClientId <= PVR_INVALID_CLIENT_ID || iClientId == PVR_VIRTUAL_CLIENT_ID)
    return bReturn;

  CSingleLock lock(m_critSection);

  PVR_CLIENTMAP_CITR itr = m_clientMap.find(iClientId);
  if (itr != m_clientMap.end() && itr->second->ReadyToUse())
  {
    addon = itr->second;
    bReturn = true;
  }
  else
  {
    CLog::Log(LOGDEBUG, "%s - client %d is not connected", __FUNCTION__, iClientId);
  }

  return bReturn;
}

bool CPVRClients::RequestRestart(AddonPtr addon, bool bDataChanged)
{
  return StopClient(addon, true);
}

bool CPVRClients::RequestRemoval(AddonPtr addon)
{
  return StopClient(addon, false);
}

void CPVRClients::Unload(void)
{
  Stop();

  CSingleLock lock(m_critSection);

  /* destroy all clients */
  for (PVR_CLIENTMAP_ITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
    itr->second->Destroy();

  /* reset class properties */
  m_bChannelScanRunning  = false;
  m_bIsPlayingLiveTV     = false;
  m_bIsPlayingRecording  = false;
  m_strPlayingClientName = StringUtils::EmptyString;

  m_clientMap.clear();
}

int CPVRClients::GetFirstConnectedClientID(void)
{
  CSingleLock lock(m_critSection);

  for (PVR_CLIENTMAP_ITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
    if (itr->second->ReadyToUse())
      return itr->second->GetID();

  return -1;
}

int CPVRClients::EnabledClientAmount(void) const
{
  int iReturn(0);
  CSingleLock lock(m_critSection);

  for (PVR_CLIENTMAP_CITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
    if (itr->second->Enabled())
      ++iReturn;

  return iReturn;
}

bool CPVRClients::HasEnabledClients(void) const
{
  return EnabledClientAmount() > 0;
}

bool CPVRClients::StopClient(AddonPtr client, bool bRestart)
{
  int iId = GetClientId(client);
  PVR_CLIENT mappedClient;
  if (GetConnectedClient(iId, mappedClient))
  {
    g_PVRManager.StopUpdateThreads();
    if (bRestart)
      mappedClient->ReCreate();
    else
      mappedClient->Destroy();
    g_PVRManager.StartUpdateThreads();

    return true;
  }

  return false;
}

int CPVRClients::ConnectedClientAmount(void) const
{
  int iReturn(0);
  CSingleLock lock(m_critSection);

  for (PVR_CLIENTMAP_CITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
    if (itr->second->ReadyToUse())
      ++iReturn;

  return iReturn;
}

bool CPVRClients::HasConnectedClients(void)
{
  return ConnectedClientAmount() > 0;
}

bool CPVRClients::GetClientName(int iClientId, CStdString &strName)
{
  bool bReturn(false);
  PVR_CLIENT client;
  if ((bReturn = GetConnectedClient(iClientId, client)) == true)
    strName = client->GetFriendlyName();

  return bReturn;
}

int CPVRClients::GetConnectedClients(PVR_CLIENTMAP &clients) const
{
  int iReturn(0);
  CSingleLock lock(m_critSection);

  for (PVR_CLIENTMAP_CITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
  {
    if (itr->second->ReadyToUse())
    {
      clients.insert(std::make_pair(itr->second->GetID(), itr->second));
      ++iReturn;
    }
  }

  return iReturn;
}

int CPVRClients::GetPlayingClientID(void) const
{
  CSingleLock lock(m_critSection);
  if (m_bIsPlayingLiveTV || m_bIsPlayingRecording)
    return m_playingClientId;
  return -EINVAL;
}

bool CPVRClients::IsPlaying(void) const
{
  CSingleLock lock(m_critSection);
  return m_bIsPlayingRecording || m_bIsPlayingLiveTV;
}

const CStdString CPVRClients::GetPlayingClientName(void) const
{
  CSingleLock lock(m_critSection);
  return m_strPlayingClientName;
}

int CPVRClients::ReadStream(void* lpBuf, int64_t uiBufSize)
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->ReadStream(lpBuf, uiBufSize);
  return -EINVAL;
}

int64_t CPVRClients::GetStreamLength(void)
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->GetStreamLength();
  return -EINVAL;
}

int64_t CPVRClients::SeekStream(int64_t iFilePosition, int iWhence/* = SEEK_SET*/)
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->SeekStream(iFilePosition, iWhence);
  return -EINVAL;
}

int64_t CPVRClients::GetStreamPosition(void)
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->GetStreamPosition();
  return -EINVAL;
}

PVR_STREAM_PROPERTIES CPVRClients::GetCurrentStreamProperties(void)
{
  PVR_STREAM_PROPERTIES props;
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    client->GetStreamProperties(&props);

  return props;
}

CStdString CPVRClients::GetCurrentInputFormat(void) const
{
  CStdString strReturn;
  CPVRChannel currentChannel;
  if (GetPlayingChannel(currentChannel))
    strReturn = currentChannel.InputFormat();

  return strReturn;
}

bool CPVRClients::IsReadingLiveStream(void) const
{
  CSingleLock lock(m_critSection);
  return m_bIsPlayingLiveTV;
}

bool CPVRClients::IsPlayingTV(void) const
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->IsPlayingLiveTV();
  return false;
}

bool CPVRClients::IsPlayingRadio(void) const
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->IsPlayingLiveRadio();
  return false;
}

bool CPVRClients::IsEncrypted(void) const
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->IsPlayingEncryptedChannel();
  return false;
}

bool CPVRClients::OpenStream(const CPVRChannel &tag)
{
  bool bReturn(false);
  CloseStream();

  /* try to open the stream on the client */
  PVR_CLIENT client;
  if (!tag.StreamURL().IsEmpty() ||
      (GetConnectedClient(tag.ClientID(), client) && client->OpenStream(tag)))
  {
    CSingleLock lock(m_critSection);
    m_playingClientId = tag.ClientID();
    m_bIsPlayingLiveTV = true;

    if (tag.ClientID() == PVR_VIRTUAL_CLIENT_ID)
      m_strPlayingClientName = g_localizeStrings.Get(19209);
    else if (!tag.IsVirtual())
      m_strPlayingClientName = client->GetFriendlyName();
    else
      m_strPlayingClientName = g_localizeStrings.Get(13205);

    bReturn = true;
  }

  return bReturn;
}

bool CPVRClients::OpenStream(const CPVRRecording &tag)
{
  bool bReturn(false);
  CloseStream();

  /* try to open the recording stream on the client */
  PVR_CLIENT client;
  if (GetConnectedClient(tag.m_iClientId, client) &&
      client->OpenStream(tag))
  {
    CSingleLock lock(m_critSection);
    m_playingClientId = tag.m_iClientId;
    m_bIsPlayingRecording = true;
    m_strPlayingClientName = client->GetFriendlyName();
    bReturn = true;
  }

  return bReturn;
}

void CPVRClients::CloseStream(void)
{
  PVR_CLIENT playingClient;
  if (GetPlayingClient(playingClient))
    playingClient->CloseStream();

  CSingleLock lock(m_critSection);
  m_bIsPlayingLiveTV     = false;
  m_bIsPlayingRecording  = false;
  m_playingClientId      = PVR_INVALID_CLIENT_ID;
  m_strPlayingClientName = StringUtils::EmptyString;
}

CStdString CPVRClients::GetStreamURL(const CPVRChannel &tag)
{
  CStdString strReturn;
  PVR_CLIENT client;
  if (GetConnectedClient(tag.ClientID(), client))
    strReturn = client->GetLiveStreamURL(tag);
  else
    CLog::Log(LOGERROR, "PVR - %s - cannot find client %d",__FUNCTION__, tag.ClientID());

  return strReturn;
}

bool CPVRClients::SwitchChannel(const CPVRChannel &channel)
{
  {
    CSingleLock lock(m_critSection);
    if (m_bIsSwitchingChannels)
    {
      CLog::Log(LOGDEBUG, "PVRClients - %s - can't switch to channel '%s'. waiting for the previous switch to complete", __FUNCTION__, channel.ChannelName().c_str());
      return false;
    }
    m_bIsSwitchingChannels = true;
  }

  bool bSwitchSuccessful(false);
  CPVRChannel currentChannel;

  if (// no channel is currently playing
      !GetPlayingChannel(currentChannel) ||
      // different backend
      currentChannel.ClientID() != channel.ClientID() ||
      // different type
      currentChannel.IsRadio() != channel.IsRadio() ||
      // stream URL should always be opened as a new file
      !channel.StreamURL().IsEmpty() || !currentChannel.StreamURL().IsEmpty())
  {
    CloseStream();

    if (channel.StreamURL().IsEmpty())
    {
      bSwitchSuccessful = OpenStream(channel);
    }
    else
    {
      CFileItem m_currentFile(channel);
      g_application.getApplicationMessenger().PlayFile(m_currentFile, false);
      bSwitchSuccessful = true;
    }
  }
  // same channel
  else if (currentChannel == channel)
  {
    bSwitchSuccessful = true;
  }
  else
  {
    PVR_CLIENT client;
    if (GetConnectedClient(channel.ClientID(), client))
      bSwitchSuccessful = client->SwitchChannel(channel);
  }

  {
    CSingleLock lock(m_critSection);
    m_bIsSwitchingChannels = false;
    if (bSwitchSuccessful)
      m_bIsValidChannelSettings = false;
  }

  if (!bSwitchSuccessful)
    CLog::Log(LOGERROR, "PVR - %s - cannot switch to channel '%s' on client '%d'",__FUNCTION__, channel.ChannelName().c_str(), channel.ClientID());

  return bSwitchSuccessful;
}

bool CPVRClients::GetPlayingChannel(CPVRChannel &channel) const
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->GetPlayingChannel(channel);
  return false;
}

bool CPVRClients::IsPlayingRecording(void) const
{
  CSingleLock lock(m_critSection);
  return m_bIsPlayingRecording;
}

bool CPVRClients::GetPlayingRecording(CPVRRecording &recording) const
{
  PVR_CLIENT client;
  if (GetPlayingClient(client))
    return client->GetPlayingRecording(recording);
  return false;
}

int CPVRClients::GetTimers(CPVRTimers *timers)
{
  int iCurSize = timers->GetNumActiveTimers();
  PVR_CLIENTMAP clients;
  GetConnectedClients(clients);

  /* get the timer list from each client */
  for (PVR_CLIENTMAP_ITR itrClients = clients.begin(); itrClients != clients.end(); itrClients++)
  {
    PVR_ERROR currentError = (*itrClients).second->GetTimers(timers);
    if (currentError != PVR_ERROR_NOT_IMPLEMENTED &&
        currentError != PVR_ERROR_NO_ERROR)
      CLog::Log(LOGERROR, "PVR - %s - cannot get timers from client '%d': %s",__FUNCTION__, (*itrClients).first, CPVRClient::ToString(currentError));
  }

  return timers->GetNumActiveTimers() - iCurSize;
}

PVR_ERROR CPVRClients::AddTimer(const CPVRTimerInfoTag &timer)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);

  PVR_CLIENT client;
  if (GetConnectedClient(timer.m_iClientId, client))
    error = client->AddTimer(timer);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot add timer to client '%d': %s",__FUNCTION__, timer.m_iClientId, CPVRClient::ToString(error));

  return error;
}

PVR_ERROR CPVRClients::UpdateTimer(const CPVRTimerInfoTag &timer)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);

  PVR_CLIENT client;
  if (GetConnectedClient(timer.m_iClientId, client))
    error = client->UpdateTimer(timer);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot update timer on client '%d': %s",__FUNCTION__, timer.m_iClientId, CPVRClient::ToString(error));

  return error;
}

PVR_ERROR CPVRClients::DeleteTimer(const CPVRTimerInfoTag &timer, bool bForce)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);
  PVR_CLIENT client;

  if (GetConnectedClient(timer.m_iClientId, client))
    error = client->DeleteTimer(timer, bForce);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot delete timer from client '%d': %s",__FUNCTION__, timer.m_iClientId, CPVRClient::ToString(error));

  return error;
}

PVR_ERROR CPVRClients::RenameTimer(const CPVRTimerInfoTag &timer, const CStdString &strNewName)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);

  PVR_CLIENT client;
  if (GetConnectedClient(timer.m_iClientId, client))
    error = client->RenameTimer(timer, strNewName);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot rename timer on client '%d': %s",__FUNCTION__, timer.m_iClientId, CPVRClient::ToString(error));

  return error;
}

int CPVRClients::GetRecordings(CPVRRecordings *recordings)
{
  int iCurSize = recordings->size();
  PVR_CLIENTMAP clients;
  GetConnectedClients(clients);

  for (PVR_CLIENTMAP_ITR itrClients = clients.begin(); itrClients != clients.end(); itrClients++)
  {
    PVR_ERROR currentError = (*itrClients).second->GetRecordings(recordings);
    if (currentError != PVR_ERROR_NOT_IMPLEMENTED &&
        currentError != PVR_ERROR_NO_ERROR)
      CLog::Log(LOGERROR, "PVR - %s - cannot get recordings from client '%d': %s",__FUNCTION__, (*itrClients).first, CPVRClient::ToString(currentError));
  }

  return recordings->size() - iCurSize;
}

PVR_ERROR CPVRClients::RenameRecording(const CPVRRecording &recording)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);

  PVR_CLIENT client;
  if (GetConnectedClient(recording.m_iClientId, client))
    error = client->RenameRecording(recording);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot rename recording on client '%d': %s",__FUNCTION__, recording.m_iClientId, CPVRClient::ToString(error));

  return error;
}

PVR_ERROR CPVRClients::DeleteRecording(const CPVRRecording &recording)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);

  PVR_CLIENT client;
  if (GetConnectedClient(recording.m_iClientId, client))
    error = client->DeleteRecording(recording);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot delete recording from client '%d': %s",__FUNCTION__, recording.m_iClientId, CPVRClient::ToString(error));

  return error;
}

bool CPVRClients::IsRecordingOnPlayingChannel(void) const
{
  CPVRChannel currentChannel;
  return GetPlayingChannel(currentChannel) &&
      currentChannel.IsRecording();
}

bool CPVRClients::CanRecordInstantly(void)
{
  CPVRChannel currentChannel;
  return GetPlayingChannel(currentChannel) &&
      currentChannel.CanRecord();
}

PVR_ERROR CPVRClients::GetEPGForChannel(const CPVRChannel &channel, CEpg *epg, time_t start, time_t end)
{
  PVR_ERROR error(PVR_ERROR_UNKNOWN);

  PVR_CLIENT client;
  if (GetConnectedClient(channel.ClientID(), client))
    error = client->GetEPGForChannel(channel, epg, start, end);

  if (error != PVR_ERROR_NO_ERROR)
    CLog::Log(LOGERROR, "PVR - %s - cannot get EPG for channel '%s' from client '%d': %s",__FUNCTION__, channel.ChannelName().c_str(), channel.ClientID(), CPVRClient::ToString(error));

  return error;
}

PVR_ERROR CPVRClients::GetChannels(CPVRChannelGroupInternal *group)
{
  PVR_ERROR error(PVR_ERROR_NO_ERROR);
  PVR_CLIENTMAP clients;
  GetConnectedClients(clients);

  /* get the channel list from each client */
  for (PVR_CLIENTMAP_ITR itrClients = clients.begin(); itrClients != clients.end(); itrClients++)
  {
    PVR_ERROR currentError = (*itrClients).second->GetChannels(*group, group->IsRadio());
    if (currentError != PVR_ERROR_NOT_IMPLEMENTED &&
        currentError != PVR_ERROR_NO_ERROR)
    {
      error = currentError;
      CLog::Log(LOGERROR, "PVR - %s - cannot get channels from client '%d': %s",__FUNCTION__, (*itrClients).first, CPVRClient::ToString(error));
    }
  }

  return error;
}

PVR_ERROR CPVRClients::GetChannelGroups(CPVRChannelGroups *groups)
{
  PVR_ERROR error(PVR_ERROR_NO_ERROR);

  PVR_CLIENTMAP clients;
  GetConnectedClients(clients);

  /* get the channel groups list from each client */
  for (PVR_CLIENTMAP_ITR itrClients = clients.begin(); itrClients != clients.end(); itrClients++)
  {
    PVR_ERROR currentError = (*itrClients).second->GetChannelGroups(groups);
    if (currentError != PVR_ERROR_NOT_IMPLEMENTED &&
        currentError != PVR_ERROR_NO_ERROR)
    {
      error = currentError;
      CLog::Log(LOGERROR, "PVR - %s - cannot get groups from client '%d': %s",__FUNCTION__, (*itrClients).first, CPVRClient::ToString(error));
    }
  }

  return error;
}

PVR_ERROR CPVRClients::GetChannelGroupMembers(CPVRChannelGroup *group)
{
  PVR_ERROR error(PVR_ERROR_NO_ERROR);
  PVR_CLIENTMAP clients;
  GetConnectedClients(clients);

  /* get the member list from each client */
  for (PVR_CLIENTMAP_ITR itrClients = clients.begin(); itrClients != clients.end(); itrClients++)
  {
    PVR_ERROR currentError = (*itrClients).second->GetChannelGroupMembers(group);
    if (currentError != PVR_ERROR_NOT_IMPLEMENTED &&
        currentError != PVR_ERROR_NO_ERROR)
    {
      error = currentError;
      CLog::Log(LOGERROR, "PVR - %s - cannot get group members from client '%d': %s",__FUNCTION__, (*itrClients).first, CPVRClient::ToString(error));
    }
  }

  return error;
}

bool CPVRClients::HasMenuHooks(int iClientID)
{
  if (iClientID < 0)
    iClientID = GetPlayingClientID();

  PVR_CLIENT client;
  return (GetConnectedClient(iClientID, client) &&
      client->HaveMenuHooks());
}

bool CPVRClients::GetMenuHooks(int iClientID, PVR_MENUHOOKS *hooks)
{
  bool bReturn(false);

  if (iClientID < 0)
    iClientID = GetPlayingClientID();

  PVR_CLIENT client;
  if (GetConnectedClient(iClientID, client) && client->HaveMenuHooks())
  {
    hooks = client->GetMenuHooks();
    bReturn = true;
  }

  return bReturn;
}

void CPVRClients::ProcessMenuHooks(int iClientID)
{
  PVR_MENUHOOKS *hooks = NULL;

  if (iClientID < 0)
    iClientID = GetPlayingClientID();

  PVR_CLIENT client;
  if (GetConnectedClient(iClientID, client) && client->HaveMenuHooks())
  {
    hooks = client->GetMenuHooks();
    std::vector<int> hookIDs;

    CGUIDialogSelect* pDialog = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
    pDialog->Reset();
    pDialog->SetHeading(19196);
    for (unsigned int i = 0; i < hooks->size(); i++)
      pDialog->Add(client->GetString(hooks->at(i).iLocalizedStringId));
    pDialog->DoModal();

    int selection = pDialog->GetSelectedLabel();
    if (selection >= 0)
      client->CallMenuHook(hooks->at(selection));
  }
}

bool CPVRClients::IsRunningChannelScan(void) const
{
  CSingleLock lock(m_critSection);
  return m_bChannelScanRunning;
}

void CPVRClients::StartChannelScan(void)
{
  PVR_CLIENTMAP clients;
  vector<PVR_CLIENT> possibleScanClients;
  PVR_CLIENT scanClient;
  CSingleLock lock(m_critSection);
  GetConnectedClients(clients);
  m_bChannelScanRunning = true;

  /* get clients that support channel scanning */
  for (PVR_CLIENTMAP_CITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
  {
    if (itr->second->SupportsChannelScan())
      possibleScanClients.push_back(itr->second);
  }

  /* multiple clients found */
  if (possibleScanClients.size() > 1)
  {
    CGUIDialogSelect* pDialog= (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);

    pDialog->Reset();
    pDialog->SetHeading(19119);

    for (unsigned int i = 0; i < possibleScanClients.size(); i++)
      pDialog->Add(possibleScanClients[i]->GetFriendlyName());

    pDialog->DoModal();

    int selection = pDialog->GetSelectedLabel();
    if (selection >= 0)
      scanClient = possibleScanClients[selection];
  }
  /* one client found */
  else if (possibleScanClients.size() == 1)
  {
    scanClient = possibleScanClients[0];
  }
  /* no clients found */
  else if (!scanClient)
  {
    CGUIDialogOK::ShowAndGetInput(19033,0,19192,0);
    return;
  }

  /* start the channel scan */
  CLog::Log(LOGNOTICE,"PVR - %s - starting to scan for channels on client %s",
      __FUNCTION__, scanClient->GetFriendlyName().c_str());
  long perfCnt = XbmcThreads::SystemClockMillis();

  /* stop the supervisor thread */
  g_PVRManager.StopUpdateThreads();

  /* do the scan */
  if (scanClient->StartChannelScan() != PVR_ERROR_NO_ERROR)
    /* an error occured */
    CGUIDialogOK::ShowAndGetInput(19111,0,19193,0);

  /* restart the supervisor thread */
  g_PVRManager.StartUpdateThreads();

  CLog::Log(LOGNOTICE, "PVRManager - %s - channel scan finished after %li.%li seconds",
      __FUNCTION__, (XbmcThreads::SystemClockMillis()-perfCnt)/1000, (XbmcThreads::SystemClockMillis()-perfCnt)%1000);
  m_bChannelScanRunning = false;
}

int CPVRClients::AddClientToDb(const AddonPtr client)
{
  /* add this client to the database if it's not in there yet */
  CPVRDatabase *database = GetPVRDatabase();
  int iClientDbId = database ? database->Persist(client) : -1;
  if (iClientDbId <= 0)
  {
    CLog::Log(LOGERROR, "PVR - %s - can't add client '%s' to the database",
        __FUNCTION__, client->Name().c_str());
  }

  return iClientDbId;
}

bool CPVRClients::IsKnownClient(const AddonPtr client) const
{
  // database IDs start at 1
  return GetClientId(client) > 0;
}

bool CPVRClients::InitialiseClient(AddonPtr client)
{
  bool bReturn(false);
  if (!client->Enabled())
    return bReturn;

  CLog::Log(LOGDEBUG, "%s - initialising add-on '%s'", __FUNCTION__, client->Name().c_str());

  /* register this client in the db */
  int iClientId = AddClientToDb(client);
  if (iClientId <= 0)
    return bReturn;

  /* load and initialise the client libraries */
  PVR_CLIENT addon;
  {
    CSingleLock lock(m_critSection);
    PVR_CLIENTMAP_CITR existingClient = m_clientMap.find(iClientId);
    if (existingClient != m_clientMap.end())
    {
      addon = existingClient->second;
    }
    else
    {
      addon = boost::dynamic_pointer_cast<CPVRClient>(client);
      m_clientMap.insert(std::make_pair(iClientId, addon));
    }
  }

  if (addon)
    bReturn = addon->Create(iClientId);

  if (!bReturn)
    CLog::Log(LOGERROR, "PVR - %s - can't initialise add-on '%s'", __FUNCTION__, client->Name().c_str());

  return bReturn;
}

bool CPVRClients::UpdateAndInitialiseClients(bool bInitialiseAllClients /* = false */)
{
  bool bReturn(true);
  ADDON::VECADDONS map;
  {
    CSingleLock lock(m_critSection);
    map = m_addons;
  }

  for (unsigned iClientPtr = 0; iClientPtr < map.size(); iClientPtr++)
  {
    const AddonPtr clientAddon = map.at(iClientPtr);

    if (!clientAddon->Enabled() && IsKnownClient(clientAddon))
    {
      /* stop the client and remove it from the db */
      bReturn &= StopClient(clientAddon, false) && bReturn;
    }
    else if (clientAddon->Enabled() && (bInitialiseAllClients || !IsKnownClient(clientAddon)))
    {
      /* register the new client and initialise it */
      bReturn &= InitialiseClient(clientAddon) && bReturn;
    }
  }

  return bReturn;
}

void CPVRClients::ResetQualityData(PVR_SIGNAL_STATUS &qualityInfo)
{
  memset(&qualityInfo, 0, sizeof(PVR_SIGNAL_STATUS));
  if (g_guiSettings.GetBool("pvrplayback.signalquality"))
  {
    strncpy(qualityInfo.strAdapterName, g_localizeStrings.Get(13205).c_str(), 1024);
    strncpy(qualityInfo.strAdapterStatus, g_localizeStrings.Get(13205).c_str(), 1024);
  }
  else
  {
    strncpy(qualityInfo.strAdapterName, g_localizeStrings.Get(13106).c_str(), 1024);
    strncpy(qualityInfo.strAdapterStatus, g_localizeStrings.Get(13106).c_str(), 1024);
  }
}

void CPVRClients::Process(void)
{
  bool bCheckedEnabledClientsOnStartup(false);

  CAddonMgr::Get().RegisterAddonMgrCallback(ADDON_PVRDLL, this);
  CAddonMgr::Get().RegisterObserver(this);

  UpdateAddons();

  while (!g_application.m_bStop && !m_bStop)
  {
    UpdateAndInitialiseClients();

    if (!bCheckedEnabledClientsOnStartup)
    {
      bCheckedEnabledClientsOnStartup = true;
      if (!HasEnabledClients())
        ShowDialogNoClientsEnabled();
    }

    Sleep(1000);
  }
}

void CPVRClients::ShowDialogNoClientsEnabled(void)
{
  CGUIDialogOK::ShowAndGetInput(19240, 19241, 19242, 19243);

  vector<CStdString> params;
  params.push_back("addons://enabled/xbmc.pvrclient");
  params.push_back("return");
  g_windowManager.ActivateWindow(WINDOW_ADDON_BROWSER, params);
}

void CPVRClients::SaveCurrentChannelSettings(void)
{
  CPVRChannel channel;
  {
    CSingleLock lock(m_critSection);
    if (!GetPlayingChannel(channel) || !m_bIsValidChannelSettings)
      return;
  }

  CPVRDatabase *database = GetPVRDatabase();
  if (!database)
    return;

  if (g_settings.m_currentVideoSettings != g_settings.m_defaultVideoSettings)
  {
    CLog::Log(LOGDEBUG, "PVR - %s - persisting custom channel settings for channel '%s'",
        __FUNCTION__, channel.ChannelName().c_str());
    database->PersistChannelSettings(channel, g_settings.m_currentVideoSettings);
  }
  else
  {
    CLog::Log(LOGDEBUG, "PVR - %s - no custom channel settings for channel '%s'",
        __FUNCTION__, channel.ChannelName().c_str());
    database->DeleteChannelSettings(channel);
  }
}

void CPVRClients::LoadCurrentChannelSettings(void)
{
  CPVRChannel channel;
  {
    CSingleLock lock(m_critSection);
    if (!GetPlayingChannel(channel))
      return;
  }

  CPVRDatabase *database = GetPVRDatabase();
  if (!database)
    return;

  if (g_application.m_pPlayer)
  {
    /* set the default settings first */
    CVideoSettings loadedChannelSettings = g_settings.m_defaultVideoSettings;

    /* try to load the settings from the database */
    database->GetChannelSettings(channel, loadedChannelSettings);

    g_settings.m_currentVideoSettings = g_settings.m_defaultVideoSettings;
    g_settings.m_currentVideoSettings.m_Brightness          = loadedChannelSettings.m_Brightness;
    g_settings.m_currentVideoSettings.m_Contrast            = loadedChannelSettings.m_Contrast;
    g_settings.m_currentVideoSettings.m_Gamma               = loadedChannelSettings.m_Gamma;
    g_settings.m_currentVideoSettings.m_Crop                = loadedChannelSettings.m_Crop;
    g_settings.m_currentVideoSettings.m_CropLeft            = loadedChannelSettings.m_CropLeft;
    g_settings.m_currentVideoSettings.m_CropRight           = loadedChannelSettings.m_CropRight;
    g_settings.m_currentVideoSettings.m_CropTop             = loadedChannelSettings.m_CropTop;
    g_settings.m_currentVideoSettings.m_CropBottom          = loadedChannelSettings.m_CropBottom;
    g_settings.m_currentVideoSettings.m_CustomPixelRatio    = loadedChannelSettings.m_CustomPixelRatio;
    g_settings.m_currentVideoSettings.m_CustomZoomAmount    = loadedChannelSettings.m_CustomZoomAmount;
    g_settings.m_currentVideoSettings.m_CustomVerticalShift = loadedChannelSettings.m_CustomVerticalShift;
    g_settings.m_currentVideoSettings.m_NoiseReduction      = loadedChannelSettings.m_NoiseReduction;
    g_settings.m_currentVideoSettings.m_Sharpness           = loadedChannelSettings.m_Sharpness;
    g_settings.m_currentVideoSettings.m_InterlaceMethod     = loadedChannelSettings.m_InterlaceMethod;
    g_settings.m_currentVideoSettings.m_OutputToAllSpeakers = loadedChannelSettings.m_OutputToAllSpeakers;
    g_settings.m_currentVideoSettings.m_AudioDelay          = loadedChannelSettings.m_AudioDelay;
    g_settings.m_currentVideoSettings.m_AudioStream         = loadedChannelSettings.m_AudioStream;
    g_settings.m_currentVideoSettings.m_SubtitleOn          = loadedChannelSettings.m_SubtitleOn;
    g_settings.m_currentVideoSettings.m_SubtitleDelay       = loadedChannelSettings.m_SubtitleDelay;
    g_settings.m_currentVideoSettings.m_CustomNonLinStretch = loadedChannelSettings.m_CustomNonLinStretch;
    g_settings.m_currentVideoSettings.m_ScalingMethod       = loadedChannelSettings.m_ScalingMethod;
    g_settings.m_currentVideoSettings.m_PostProcess         = loadedChannelSettings.m_PostProcess;
    g_settings.m_currentVideoSettings.m_DeinterlaceMode     = loadedChannelSettings.m_DeinterlaceMode;

    /* only change the view mode if it's different */
    if (g_settings.m_currentVideoSettings.m_ViewMode != loadedChannelSettings.m_ViewMode)
    {
      g_settings.m_currentVideoSettings.m_ViewMode = loadedChannelSettings.m_ViewMode;

      #ifdef HAS_VIDEO_PLAYBACK
      g_renderManager.SetViewMode(g_settings.m_currentVideoSettings.m_ViewMode);
      #endif
      g_settings.m_currentVideoSettings.m_CustomZoomAmount = g_settings.m_fZoomAmount;
      g_settings.m_currentVideoSettings.m_CustomPixelRatio = g_settings.m_fPixelRatio;
    }

    /* only change the subtitle stream, if it's different */
    if (g_settings.m_currentVideoSettings.m_SubtitleStream != loadedChannelSettings.m_SubtitleStream)
    {
      g_settings.m_currentVideoSettings.m_SubtitleStream = loadedChannelSettings.m_SubtitleStream;

      g_application.m_pPlayer->SetSubtitle(g_settings.m_currentVideoSettings.m_SubtitleStream);
    }

    /* only change the audio stream if it's different */
    if (g_application.m_pPlayer->GetAudioStream() != g_settings.m_currentVideoSettings.m_AudioStream)
      g_application.m_pPlayer->SetAudioStream(g_settings.m_currentVideoSettings.m_AudioStream);

    g_application.m_pPlayer->SetAVDelay(g_settings.m_currentVideoSettings.m_AudioDelay);
    g_application.m_pPlayer->SetDynamicRangeCompression((long)(g_settings.m_currentVideoSettings.m_VolumeAmplification * 100));
    g_application.m_pPlayer->SetSubtitleVisible(g_settings.m_currentVideoSettings.m_SubtitleOn);
    g_application.m_pPlayer->SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

    /* settings can be saved on next channel switch */
    m_bIsValidChannelSettings = true;
  }
}

bool CPVRClients::UpdateAddons(void)
{
  ADDON::VECADDONS addons;
  bool bReturn(CAddonMgr::Get().GetAddons(ADDON_PVRDLL, addons, true, false));

  if (bReturn)
  {
    CSingleLock lock(m_critSection);
    m_addons = addons;
  }

  return bReturn;
}

void CPVRClients::Notify(const Observable &obs, const CStdString& msg)
{
  UpdateAddons();
  UpdateAndInitialiseClients();
}

bool CPVRClients::GetClient(const CStdString &strId, ADDON::AddonPtr &addon) const
{
  CSingleLock lock(m_critSection);
  for (PVR_CLIENTMAP_CITR itr = m_clientMap.begin(); itr != m_clientMap.end(); itr++)
  {
    if (itr->second->ID() == strId)
    {
      addon = itr->second;
      return true;
    }
  }
  return false;
}

bool CPVRClients::GetPlayingClient(PVR_CLIENT &client) const
{
  CSingleLock lock(m_critSection);
  return GetConnectedClient(GetPlayingClientID(), client);
}

bool CPVRClients::SupportsEPG(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsEPG();
  return false;
}

bool CPVRClients::SupportsTV(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsTV();
  return false;
}

bool CPVRClients::SupportsRadio(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsRadio();
  return false;
}

bool CPVRClients::SupportsRecordings(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsRecordings();
  return false;
}

bool CPVRClients::SupportsTimers(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsTimers();
  return false;
}

bool CPVRClients::SupportsChannelGroups(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsChannelGroups();
  return false;
}

bool CPVRClients::SupportsChannelScan(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsChannelScan();
  return false;
}

bool CPVRClients::SupportsRecordingFolders(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->SupportsRecordingFolders();
  return false;
}

bool CPVRClients::HandlesInputStream(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->HandlesInputStream();
  return false;
}

bool CPVRClients::HandlesDemuxing(int iClientId) const
{
  PVR_CLIENT client;
  if (GetConnectedClient(iClientId, client))
    return client->HandlesDemuxing();
  return false;
}
