package hela.eillance.screens

import androidx.compose.runtime.Composable
import hela.eillance.MainViewModel
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import hela.eillance.viewmodels.SettingsViewModel

@Composable
fun OneOnOneConversationScreen(
  chatVM: ConversationViewModel,
  contactsVM: ContactsViewModel,
  settingsVM: SettingsViewModel,
  callVM: CallViewModel,
  targetMessageId: Long?,
  mainViewModel: MainViewModel,
  peerId: String,
  contact: Contact?,
  myId: String,
  historyPage: HistoryPage,
  callState: CallState,
  connectionState: ServiceConnectionState,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit,
  onNavigateToMedia: () -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit
) {
  BaseConversationScreen(
    chatVM = chatVM,
    contactsVM = contactsVM,
    settingsVM = settingsVM,
    callVM = callVM,
    targetMessageId = targetMessageId,
    mainViewModel = mainViewModel,
    peerId = peerId,
    contact = contact,
    myId = myId,
    historyPage = historyPage,
    callState = callState,
    connectionState = connectionState,
    onBack = onBack,
    onNavigateToCall = onNavigateToCall,
    onShowContactProfile = onShowContactProfile,
    onNavigateToMedia = onNavigateToMedia,
    onMediaClick = onMediaClick,
    showConnectionStatus = true,
    isGroupChat = false,
    showCallButtons = true
  )
}

@Composable
fun GroupConversationScreen(
  chatVM: ConversationViewModel,
  contactsVM: ContactsViewModel,
  settingsVM: SettingsViewModel,
  callVM: CallViewModel,
  targetMessageId: Long?,
  mainViewModel: MainViewModel,
  peerId: String,
  contact: Contact?,
  myId: String,
  historyPage: HistoryPage,
  callState: CallState,
  connectionState: ServiceConnectionState,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit,
  onNavigateToMedia: () -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit
) {
  BaseConversationScreen(
    chatVM = chatVM,
    contactsVM = contactsVM,
    settingsVM = settingsVM,
    callVM = callVM,
    targetMessageId = targetMessageId,
    mainViewModel = mainViewModel,
    peerId = peerId,
    contact = contact,
    myId = myId,
    historyPage = historyPage,
    callState = callState,
    connectionState = connectionState,
    onBack = onBack,
    onNavigateToCall = onNavigateToCall,
    onShowContactProfile = onShowContactProfile,
    onNavigateToMedia = onNavigateToMedia,
    onMediaClick = onMediaClick,
    showConnectionStatus = false,
    isGroupChat = true,
    showCallButtons = false
  )
}

@Composable
fun ChannelConversationScreen(
  chatVM: ConversationViewModel,
  contactsVM: ContactsViewModel,
  settingsVM: SettingsViewModel,
  callVM: CallViewModel,
  targetMessageId: Long?,
  mainViewModel: MainViewModel,
  peerId: String,
  contact: Contact?,
  myId: String,
  historyPage: HistoryPage,
  callState: CallState,
  connectionState: ServiceConnectionState,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit,
  onNavigateToMedia: () -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit
) {
  BaseConversationScreen(
    chatVM = chatVM,
    contactsVM = contactsVM,
    settingsVM = settingsVM,
    callVM = callVM,
    targetMessageId = targetMessageId,
    mainViewModel = mainViewModel,
    peerId = peerId,
    contact = contact,
    myId = myId,
    historyPage = historyPage,
    callState = callState,
    connectionState = connectionState,
    onBack = onBack,
    onNavigateToCall = onNavigateToCall,
    onShowContactProfile = onShowContactProfile,
    onNavigateToMedia = onNavigateToMedia,
    onMediaClick = onMediaClick,
    showConnectionStatus = false,
    isGroupChat = false,
    showCallButtons = false
  )
}
