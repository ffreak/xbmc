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

#include "PVRClient.h"

#include "epg/EpgInfoTag.h"
#include "pvr/channels/PVRChannelGroups.h"
#include "pvr/timers/PVRTimerInfoTag.h"
#include "pvr/recordings/PVRRecording.h"
#include "settings/AdvancedSettings.h"
#include "utils/StringUtils.h"

using namespace std;
using namespace ADDON;
using namespace PVR;
using namespace EPG;

#define DEFAULT_INFO_STRING_VALUE "unknown"

CPVRClient::CPVRClient(const AddonProps& props) :
    CAddonDll<DllPVRClient, PVRClient, PVR_PROPERTIES>(props)
{
  ResetProperties();
}

CPVRClient::CPVRClient(const cp_extension_t *ext) :
    CAddonDll<DllPVRClient, PVRClient, PVR_PROPERTIES>(ext)
{
  ResetProperties();
}

CPVRClient::~CPVRClient(void)
{
  Destroy();
  if (m_pInfo)
    SAFE_DELETE(m_pInfo);
}

void CPVRClient::ResetProperties(int iClientId /* = PVR_INVALID_CLIENT_ID */)
{
  CSingleLock lock(m_critSection);

  /* initialise members */
  if (m_pInfo)
    SAFE_DELETE(m_pInfo);
  m_pInfo                 = new PVR_PROPERTIES;
  CStdString userpath     = CSpecialProtocol::TranslatePath(Profile());
  m_pInfo->strUserPath    = userpath.c_str();
  CStdString clientpath   = CSpecialProtocol::TranslatePath(Path());
  m_pInfo->strClientPath  = clientpath.c_str();

  m_menuhooks.clear();
  m_bReadyToUse           = false;
  m_iClientId             = PVR_INVALID_CLIENT_ID;
  m_strBackendVersion     = DEFAULT_INFO_STRING_VALUE;
  m_strConnectionString   = DEFAULT_INFO_STRING_VALUE;
  m_strFriendlyName       = DEFAULT_INFO_STRING_VALUE;
  m_strBackendName        = DEFAULT_INFO_STRING_VALUE;
  ResetAddonCapabilities(m_addonCapabilities);
}

void CPVRClient::ResetAddonCapabilities(PVR_ADDON_CAPABILITIES &addonCapabilities)
{
  CSingleLock lock(m_critSection);
  addonCapabilities.bSupportsEPG              = false;
  addonCapabilities.bSupportsTV               = false;
  addonCapabilities.bSupportsRadio            = false;
  addonCapabilities.bSupportsRecordings       = false;
  addonCapabilities.bSupportsTimers           = false;
  addonCapabilities.bSupportsChannelGroups    = false;
  addonCapabilities.bSupportsChannelScan      = false;
  addonCapabilities.bHandlesInputStream       = false;
  addonCapabilities.bHandlesDemuxing          = false;
  addonCapabilities.bSupportsRecordingFolders = false;
}

bool CPVRClient::GetAddonProperties(void)
{
  CStdString strHostName, strBackendName, strConnectionString, strFriendlyName, strBackendVersion;
  PVR_ADDON_CAPABILITIES addonCapabilities;

  /* get the capabilities */
  try
  {
    ResetAddonCapabilities(addonCapabilities);
    PVR_ERROR retVal = m_pStruct->GetAddonCapabilities(&addonCapabilities);
    if (retVal != PVR_ERROR_NO_ERROR)
    {
      CLog::Log(LOGERROR, "PVR - couldn't get the capabilities for add-on '%s'. Please contact the developer of this add-on: %s", GetFriendlyName().c_str(), Author().c_str());
      return false;
    }
  }
  catch (exception &e) { LogException(e, "GetAddonCapabilities()"); return false; }

  /* get the name of the backend */
  try { strBackendName = m_pStruct->GetBackendName(); }
  catch (exception &e) { LogException(e, "GetBackendName()"); return false;  }

  /* get the connection string */
  try { strConnectionString = m_pStruct->GetConnectionString(); }
  catch (exception &e) { LogException(e, "GetConnectionString()"); return false;  }

  /* display name = backend name:connection string */
  strFriendlyName.Format("%s:%s", strBackendName.c_str(), strConnectionString.c_str());

  /* backend version number */
  try { strBackendVersion = m_pStruct->GetBackendVersion(); }
  catch (exception &e) { LogException(e, "GetBackendVersion()"); return false;  }

  /* update the members */
  CSingleLock lock(m_critSection);
  m_strBackendName      = strBackendName;
  m_strConnectionString = strConnectionString;
  m_strFriendlyName     = strFriendlyName;
  m_strBackendVersion   = strBackendVersion;
  m_addonCapabilities   = addonCapabilities;

  return true;
}

bool CPVRClient::Create(int iClientId)
{
  /* ensure that a previous instance is destroyed */
  Destroy();

  /* reset all properties to defaults */
  ResetProperties(iClientId);

  /* initialise the add-on */
  bool bReadyToUse(false);
  CLog::Log(LOGDEBUG, "PVR - %s - creating PVR add-on instance '%s'", __FUNCTION__, Name().c_str());
  try
  {
    if (CAddonDll<DllPVRClient, PVRClient, PVR_PROPERTIES>::Create())
      bReadyToUse = GetAddonProperties();
  }
  catch (exception &e) { LogException(e, "Create()"); }

  CSingleLock lock(m_critSection);
  m_bReadyToUse = bReadyToUse;
  m_iClientId   = iClientId;
  return bReadyToUse;
}

void CPVRClient::Destroy(void)
{
  if (!ReadyToUse())
    return;

  /* reset 'ready to use' to false */
  {
    CSingleLock lock(m_critSection);
    CLog::Log(LOGDEBUG, "PVR - %s - destroying PVR add-on '%s'", __FUNCTION__, GetFriendlyName().c_str());
    m_bReadyToUse = false;
  }

  /* destroy the add-on */
  try { CAddonDll<DllPVRClient, PVRClient, PVR_PROPERTIES>::Destroy(); }
  catch (exception &e) { LogException(e, "Destroy()"); }

  /* reset all properties to defaults */
  ResetProperties();
}

void CPVRClient::ReCreate(void)
{
  int iClientID(PVR_INVALID_CLIENT_ID);
  {
    CSingleLock lock(m_critSection);
    if (m_pInfo)
      iClientID = m_iClientId;
  }

  /* recreate the instance */
  if (iClientID != PVR_INVALID_CLIENT_ID)
    Create(iClientID);
}

bool CPVRClient::ReadyToUse(void) const
{
  CSingleLock lock(m_critSection);
  return m_bReadyToUse;
}

int CPVRClient::GetID(void) const
{
  CSingleLock lock(m_critSection);
  return m_iClientId;
}

void CPVRClient::WriteClientGroupInfo(const CPVRChannelGroup &xbmcGroup, PVR_CHANNEL_GROUP &addonGroup)
{
  addonGroup.bIsRadio     = xbmcGroup.IsRadio();
  addonGroup.strGroupName = xbmcGroup.GroupName();
}

void CPVRClient::WriteClientRecordingInfo(const CPVRRecording &xbmcRecording, PVR_RECORDING &addonRecording)
{
  time_t recTime;
  xbmcRecording.RecordingTimeAsUTC().GetAsTime(recTime);

  addonRecording.recordingTime  = recTime - g_advancedSettings.m_iPVRTimeCorrection;
  addonRecording.strRecordingId = xbmcRecording.m_strRecordingId.c_str();
  addonRecording.strTitle       = xbmcRecording.m_strTitle.c_str();
  addonRecording.strPlotOutline = xbmcRecording.m_strPlotOutline.c_str();
  addonRecording.strPlot        = xbmcRecording.m_strPlot.c_str();
  addonRecording.strChannelName = xbmcRecording.m_strChannelName.c_str();
  addonRecording.iDuration      = xbmcRecording.GetDuration();
  addonRecording.iPriority      = xbmcRecording.m_iPriority;
  addonRecording.iLifetime      = xbmcRecording.m_iLifetime;
  addonRecording.strDirectory   = xbmcRecording.m_strDirectory.c_str();
  addonRecording.strStreamURL   = xbmcRecording.m_strStreamURL.c_str();
}

void CPVRClient::WriteClientTimerInfo(const CPVRTimerInfoTag &xbmcTimer, PVR_TIMER &addonTimer)
{
  time_t start, end, firstDay;
  xbmcTimer.StartAsUTC().GetAsTime(start);
  xbmcTimer.EndAsUTC().GetAsTime(end);
  xbmcTimer.FirstDayAsUTC().GetAsTime(firstDay);
  CEpgInfoTag *epgTag = xbmcTimer.GetEpgInfoTag();

  addonTimer.iClientIndex      = xbmcTimer.m_iClientIndex;
  addonTimer.state             = xbmcTimer.m_state;
  addonTimer.iClientIndex      = xbmcTimer.m_iClientIndex;
  addonTimer.iClientChannelUid = xbmcTimer.m_iClientChannelUid;
  addonTimer.strTitle          = xbmcTimer.m_strTitle;
  addonTimer.strDirectory      = xbmcTimer.m_strDirectory;
  addonTimer.iPriority         = xbmcTimer.m_iPriority;
  addonTimer.iLifetime         = xbmcTimer.m_iLifetime;
  addonTimer.bIsRepeating      = xbmcTimer.m_bIsRepeating;
  addonTimer.iWeekdays         = xbmcTimer.m_iWeekdays;
  addonTimer.startTime         = start - g_advancedSettings.m_iPVRTimeCorrection;
  addonTimer.endTime           = end - g_advancedSettings.m_iPVRTimeCorrection;
  addonTimer.firstDay          = firstDay - g_advancedSettings.m_iPVRTimeCorrection;
  addonTimer.iEpgUid           = epgTag ? epgTag->UniqueBroadcastID() : -1;
  addonTimer.strSummary        = xbmcTimer.m_strSummary.c_str();
  addonTimer.iMarginStart      = xbmcTimer.m_iMarginStart;
  addonTimer.iMarginEnd        = xbmcTimer.m_iMarginEnd;
  addonTimer.iGenreType        = xbmcTimer.m_iGenreType;
  addonTimer.iGenreSubType     = xbmcTimer.m_iGenreSubType;
}

void CPVRClient::WriteClientChannelInfo(const CPVRChannel &xbmcChannel, PVR_CHANNEL &addonChannel)
{
  addonChannel.iUniqueId         = xbmcChannel.UniqueID();
  addonChannel.iChannelNumber    = xbmcChannel.ClientChannelNumber();
  addonChannel.strChannelName    = xbmcChannel.ClientChannelName().c_str();
  addonChannel.strIconPath       = xbmcChannel.IconPath().c_str();
  addonChannel.iEncryptionSystem = xbmcChannel.EncryptionSystem();
  addonChannel.bIsRadio          = xbmcChannel.IsRadio();
  addonChannel.bIsHidden         = xbmcChannel.IsHidden();
  addonChannel.strInputFormat    = xbmcChannel.InputFormat().c_str();
  addonChannel.strStreamURL      = xbmcChannel.StreamURL().c_str();
}

PVR_ADDON_CAPABILITIES CPVRClient::GetAddonCapabilities(void) const
{
  CSingleLock lock(m_critSection);
  PVR_ADDON_CAPABILITIES addonCapabilities(m_addonCapabilities);
  return addonCapabilities;
}

CStdString CPVRClient::GetBackendName(void) const
{
  CSingleLock lock(m_critSection);
  CStdString strReturn(m_strBackendName);
  return strReturn;
}

CStdString CPVRClient::GetBackendVersion(void) const
{
  CSingleLock lock(m_critSection);
  CStdString strReturn(m_strBackendVersion);
  return strReturn;
}

CStdString CPVRClient::GetConnectionString(void) const
{
  CSingleLock lock(m_critSection);
  CStdString strReturn(m_strConnectionString);
  return strReturn;
}

CStdString CPVRClient::GetFriendlyName(void) const
{
  CSingleLock lock(m_critSection);
  CStdString strReturn(m_strFriendlyName);
  return strReturn;
}

PVR_ERROR CPVRClient::GetDriveSpace(long long *iTotal, long long *iUsed)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  try { return m_pStruct->GetDriveSpace(iTotal, iUsed); }
  catch (exception &e) { LogException(e, "GetDriveSpace()"); }

  /* default to 0 on error */
  *iTotal = 0;
  *iUsed  = 0;

  return PVR_ERROR_UNKNOWN;
}

PVR_ERROR CPVRClient::StartChannelScan(void)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsChannelScan)
    return PVR_ERROR_NOT_IMPLEMENTED;

  try { return m_pStruct->DialogChannelScan(); }
  catch (exception &e) { LogException(e, "DialogChannelScan()"); }

  return PVR_ERROR_UNKNOWN;
}

void CPVRClient::CallMenuHook(const PVR_MENUHOOK &hook)
{
  if (ReadyToUse())
  {
    try { m_pStruct->MenuHook(hook); }
    catch (exception &e) { LogException(e, "MenuHook()"); }
  }
}

PVR_ERROR CPVRClient::GetEPGForChannel(const CPVRChannel &channel, CEpg *epg, time_t start /* = 0 */, time_t end /* = 0 */, bool bSaveInDb /* = false*/)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsEPG)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_CHANNEL addonChannel;
    WriteClientChannelInfo(channel, addonChannel);

    ADDON_HANDLE_STRUCT handle;
    handle.callerAddress = this;
    handle.dataAddress = epg;
    handle.dataIdentifier = bSaveInDb ? 1 : 0; // used by the callback method CAddonCallbacksPVR::PVRTransferEpgEntry()
    retVal = m_pStruct->GetEpg(&handle,
        addonChannel,
        start ? start - g_advancedSettings.m_iPVRTimeCorrection : 0,
        end ? end - g_advancedSettings.m_iPVRTimeCorrection : 0);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "GetEpg()");
  }

  return retVal;
}

int CPVRClient::GetChannelGroupsAmount(void)
{
  int iReturn(-EINVAL);

  if (ReadyToUse() && m_addonCapabilities.bSupportsChannelGroups)
  {
    try { iReturn = m_pStruct->GetChannelGroupsAmount(); }
    catch (exception &e) { LogException(e, "GetChannelGroupsAmount()"); }
  }

  return iReturn;
}

PVR_ERROR CPVRClient::GetChannelGroups(CPVRChannelGroups *groups)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsChannelGroups)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    ADDON_HANDLE_STRUCT handle;
    handle.callerAddress = this;
    handle.dataAddress = groups;
    retVal = m_pStruct->GetChannelGroups(&handle, groups->IsRadio());

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "GetChannelGroups()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::GetChannelGroupMembers(CPVRChannelGroup *group)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsChannelGroups)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    ADDON_HANDLE_STRUCT handle;
    handle.callerAddress = this;
    handle.dataAddress = group;

    PVR_CHANNEL_GROUP tag;
    WriteClientGroupInfo(*group, tag);

    CLog::Log(LOGDEBUG, "PVR - %s - get group members for group '%s' from add-on '%s'",
        __FUNCTION__, tag.strGroupName, GetFriendlyName().c_str());
    retVal = m_pStruct->GetChannelGroupMembers(&handle, tag);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "GetChannelGroupMembers()");
  }

  return retVal;
}

int CPVRClient::GetChannelsAmount(void)
{
  int iReturn(-EINVAL);

  if (ReadyToUse() && (m_addonCapabilities.bSupportsTV || m_addonCapabilities.bSupportsRadio))
  {
    try { iReturn = m_pStruct->GetChannelsAmount(); }
    catch (exception &e) { LogException(e, "GetChannelsAmount()"); }
  }

  return iReturn;
}

PVR_ERROR CPVRClient::GetChannels(CPVRChannelGroup &channels, bool radio)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if ((!m_addonCapabilities.bSupportsRadio && radio) ||
      (!m_addonCapabilities.bSupportsTV && !radio))
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    ADDON_HANDLE_STRUCT handle;
    handle.callerAddress = this;
    handle.dataAddress = (CPVRChannelGroup*) &channels;
    retVal = m_pStruct->GetChannels(&handle, radio);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "GetChannels()");
  }

  return retVal;
}

int CPVRClient::GetRecordingsAmount(void)
{
  int iReturn(-EINVAL);

  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    try { iReturn = m_pStruct->GetRecordingsAmount(); }
    catch (exception &e) { LogException(e, "GetRecordingsAmount()"); }
  }

  return iReturn;
}

PVR_ERROR CPVRClient::GetRecordings(CPVRRecordings *results)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsRecordings)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    ADDON_HANDLE_STRUCT handle;
    handle.callerAddress = this;
    handle.dataAddress = (CPVRRecordings*) results;
    retVal = m_pStruct->GetRecordings(&handle);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "GetRecordings()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::DeleteRecording(const CPVRRecording &recording)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsRecordings)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_RECORDING tag;
    WriteClientRecordingInfo(recording, tag);

    retVal = m_pStruct->DeleteRecording(tag);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "DeleteRecording()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::RenameRecording(const CPVRRecording &recording)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsRecordings)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_RECORDING tag;
    WriteClientRecordingInfo(recording, tag);

    retVal = m_pStruct->RenameRecording(tag);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "RenameRecording()");
  }

  return retVal;
}

int CPVRClient::GetTimersAmount(void)
{
  int iReturn(-EINVAL);

  if (ReadyToUse() && m_addonCapabilities.bSupportsTimers)
  {
    try { iReturn = m_pStruct->GetTimersAmount(); }
    catch (exception &e) { LogException(e, "GetTimersAmount()"); }
  }

  return iReturn;
}

PVR_ERROR CPVRClient::GetTimers(CPVRTimers *results)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsTimers)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    ADDON_HANDLE_STRUCT handle;
    handle.callerAddress = this;
    handle.dataAddress = (CPVRTimers*) results;
    retVal = m_pStruct->GetTimers(&handle);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "GetTimers()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::AddTimer(const CPVRTimerInfoTag &timer)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsTimers)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_TIMER tag;
    WriteClientTimerInfo(timer, tag);

    retVal = m_pStruct->AddTimer(tag);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "AddTimer()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::DeleteTimer(const CPVRTimerInfoTag &timer, bool bForce /* = false */)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsTimers)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_TIMER tag;
    WriteClientTimerInfo(timer, tag);

    retVal = m_pStruct->DeleteTimer(tag, bForce);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "DeleteTimer()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::RenameTimer(const CPVRTimerInfoTag &timer, const CStdString &strNewName)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsTimers)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_TIMER tag;
    WriteClientTimerInfo(timer, tag);

    retVal = m_pStruct->UpdateTimer(tag);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "UpdateTimer()");
  }

  return retVal;
}

PVR_ERROR CPVRClient::UpdateTimer(const CPVRTimerInfoTag &timer)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  if (!m_addonCapabilities.bSupportsTimers)
    return PVR_ERROR_NOT_IMPLEMENTED;

  PVR_ERROR retVal(PVR_ERROR_UNKNOWN);
  try
  {
    PVR_TIMER tag;
    WriteClientTimerInfo(timer, tag);

    retVal = m_pStruct->UpdateTimer(tag);

    LogError(retVal, __FUNCTION__);
  }
  catch (exception &e)
  {
    LogException(e, "UpdateTimer()");
  }

  return retVal;
}

bool CPVRClient::OpenLiveStream(const CPVRChannel &channel)
{
  bool bReturn(false);

  if (CanPlayChannel(channel))
  {
    try
    {
      PVR_CHANNEL tag;
      WriteClientChannelInfo(channel, tag);
      bReturn = m_pStruct->OpenLiveStream(tag);
    }
    catch (exception &e)
    {
      LogException(e, "OpenLiveStream()");
    }
  }

  return bReturn;
}

void CPVRClient::CloseLiveStream(void)
{
  if (ReadyToUse())
  {
    try { m_pStruct->CloseLiveStream(); }
    catch (exception &e) { LogException(e, "CloseLiveStream()"); }
  }
}

int CPVRClient::ReadLiveStream(void* lpBuf, int64_t uiBufSize)
{
  if (ReadyToUse())
  {
    try { return m_pStruct->ReadLiveStream((unsigned char *)lpBuf, (int)uiBufSize); }
    catch (exception &e) { LogException(e, "ReadLiveStream()"); }
  }

  return -EINVAL;
}

int64_t CPVRClient::SeekLiveStream(int64_t iFilePosition, int iWhence/* = SEEK_SET*/)
{
  if (ReadyToUse())
  {
    try { return m_pStruct->SeekLiveStream(iFilePosition, iWhence); }
    catch (exception &e) { LogException(e, "SeekLiveStream()"); }
  }

  return -EINVAL;
}

int64_t CPVRClient::PositionLiveStream(void)
{
  if (ReadyToUse())
  {
    try { return m_pStruct->PositionLiveStream(); }
    catch (exception &e) { LogException(e, "PositionLiveStream()"); }
  }

  return -EINVAL;
}

int64_t CPVRClient::LengthLiveStream(void)
{
  if (ReadyToUse())
  {
    try { return m_pStruct->LengthLiveStream(); }
    catch (exception &e) { LogException(e, "LengthLiveStream()"); }
  }

  return -EINVAL;
}

int CPVRClient::GetCurrentClientChannel(void)
{
  if (ReadyToUse())
  {
    try { return m_pStruct->GetCurrentClientChannel(); }
    catch (exception &e) { LogException(e, "GetCurrentClientChannel()"); }
  }

  return -EINVAL;
}

bool CPVRClient::SwitchChannel(const CPVRChannel &channel)
{
  if (CanPlayChannel(channel))
  {
    PVR_CHANNEL tag;
    WriteClientChannelInfo(channel, tag);
    try { return m_pStruct->SwitchChannel(tag); }
    catch (exception &e) { LogException(e, "SwitchChannel()"); }
  }

  return false;
}

bool CPVRClient::SignalQuality(PVR_SIGNAL_STATUS &qualityinfo)
{
  if (ReadyToUse())
  {
    try
    {
      PVR_ERROR retVal = m_pStruct->SignalStatus(qualityinfo);
      if (LogError(retVal, __FUNCTION__))
        return true;
    }
    catch (exception &e)
    {
      LogException(e, "SignalStatus()");
    }
  }

  return false;
}

CStdString CPVRClient::GetLiveStreamURL(const CPVRChannel &channel)
{
  if (CanPlayChannel(channel))
  {
    try
    {
      PVR_CHANNEL tag;
      WriteClientChannelInfo(channel, tag);
      return m_pStruct->GetLiveStreamURL(tag);
    }
    catch (exception &e)
    {
      LogException(e, "GetLiveStreamURL()");
    }
  }

  return StringUtils::EmptyString;
}

bool CPVRClient::OpenRecordedStream(const CPVRRecording &recording)
{
  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    PVR_RECORDING tag;
    WriteClientRecordingInfo(recording, tag);

    try { return m_pStruct->OpenRecordedStream(tag); }
    catch (exception &e) { LogException(e, "OpenRecordedStream()"); }
  }

  return false;
}

void CPVRClient::CloseRecordedStream(void)
{
  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    try { return m_pStruct->CloseRecordedStream(); }
    catch (exception &e) { LogException(e, "CloseRecordedStream()"); }
  }
}

int CPVRClient::ReadRecordedStream(void* lpBuf, int64_t uiBufSize)
{
  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    try { return m_pStruct->ReadRecordedStream((unsigned char *)lpBuf, (int)uiBufSize); }
    catch (exception &e) { LogException(e, "ReadRecordedStream()"); }
  }

  return -EINVAL;
}

int64_t CPVRClient::SeekRecordedStream(int64_t iFilePosition, int iWhence/* = SEEK_SET*/)
{
  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    try { return m_pStruct->SeekRecordedStream(iFilePosition, iWhence); }
    catch (exception &e) { LogException(e, "SeekRecordedStream()"); }
  }

  return -EINVAL;
}

int64_t CPVRClient::PositionRecordedStream()
{
  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    try { return m_pStruct->PositionRecordedStream(); }
    catch (exception &e) { LogException(e, "PositionRecordedStream()"); }
  }

  return -EINVAL;
}

int64_t CPVRClient::LengthRecordedStream(void)
{
  if (ReadyToUse() && m_addonCapabilities.bSupportsRecordings)
  {
    try { return m_pStruct->LengthRecordedStream(); }
    catch (exception &e) { LogException(e, "LengthRecordedStream()"); }
  }

  return -EINVAL;
}

PVR_ERROR CPVRClient::GetStreamProperties(PVR_STREAM_PROPERTIES *props)
{
  if (!ReadyToUse())
    return PVR_ERROR_NOT_POSSIBLE;

  try { return m_pStruct->GetStreamProperties(props); }
  catch (exception &e) { LogException(e, "GetStreamProperties()"); }

  return PVR_ERROR_UNKNOWN;
}

void CPVRClient::DemuxReset(void)
{
  if (ReadyToUse() && m_addonCapabilities.bHandlesDemuxing)
  {
    try { m_pStruct->DemuxReset(); }
    catch (exception &e) { LogException(e, "DemuxReset()"); }
  }
}

void CPVRClient::DemuxAbort(void)
{
  if (ReadyToUse() && m_addonCapabilities.bHandlesDemuxing)
  {
    try { m_pStruct->DemuxAbort(); }
    catch (exception &e) { LogException(e, "DemuxAbort()"); }
  }
}

void CPVRClient::DemuxFlush(void)
{
  if (ReadyToUse() && m_addonCapabilities.bHandlesDemuxing)
  {
    try { m_pStruct->DemuxFlush(); }
    catch (exception &e) { LogException(e, "DemuxFlush()"); }
  }
}

DemuxPacket* CPVRClient::DemuxRead(void)
{
  if (ReadyToUse() && m_addonCapabilities.bHandlesDemuxing)
  {
    try { return m_pStruct->DemuxRead(); }
    catch (exception &e) { LogException(e, "DemuxRead()"); }
  }

  return NULL;
}

bool CPVRClient::HaveMenuHooks(void) const
{
  CSingleLock lock(m_critSection);
  return ReadyToUse() ? m_menuhooks.size() > 0 : false;
}

PVR_MENUHOOKS *CPVRClient::GetMenuHooks(void)
{
  CSingleLock lock(m_critSection);
  return ReadyToUse() ? &m_menuhooks : NULL;
}

const char *CPVRClient::ToString(const PVR_ERROR error)
{
  switch (error)
  {
  case PVR_ERROR_NO_ERROR:
    return "no error";
  case PVR_ERROR_NOT_IMPLEMENTED:
    return "not implemented";
  case PVR_ERROR_SERVER_ERROR:
    return "server error";
  case PVR_ERROR_SERVER_TIMEOUT:
    return "server timeout";
  case PVR_ERROR_NOT_SYNC:
    return "timers not synced";
  case PVR_ERROR_NOT_DELETED:
    return "not deleted";
  case PVR_ERROR_NOT_SAVED:
    return "not saved";
  case PVR_ERROR_RECORDING_RUNNING:
    return "recording already running";
  case PVR_ERROR_ALREADY_PRESENT:
    return "already present";
  case PVR_ERROR_NOT_POSSIBLE:
    return "not possible";
  case PVR_ERROR_UNKNOWN:
  default:
    return "unknown error";
  }
}

bool CPVRClient::LogError(const PVR_ERROR &error, const char *strMethod)
{
  if (error != PVR_ERROR_NO_ERROR)
  {
    CLog::Log(LOGERROR, "PVR - %s - add-on '%s' returned an error: %s", strMethod, GetFriendlyName().c_str(), ToString(error));
    return false;
  }
  return true;
}

void CPVRClient::LogException(const exception &e, const char *strFunctionName)
{
  CLog::Log(LOGERROR, "PVR - exception '%s' caught while trying to call '%s' on add-on '%s'. Please contact the developer of this add-on: %s", e.what(), strFunctionName, GetFriendlyName().c_str(), Author().c_str());
}

bool CPVRClient::CanPlayChannel(const CPVRChannel &channel) const
{
  return (ReadyToUse() &&
           ((m_addonCapabilities.bSupportsTV && !channel.IsRadio()) ||
            (m_addonCapabilities.bSupportsRadio && channel.IsRadio())));
}
