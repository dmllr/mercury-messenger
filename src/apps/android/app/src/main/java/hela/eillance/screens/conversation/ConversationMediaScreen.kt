package hela.eillance.screens.conversation

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import coil.compose.AsyncImage
import coil.request.videoFrameMillis
import hela.eillance.camera.R
import hela.eillance.components.FileAttachmentItem
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentInfo
import hela.eillance.ndkio.AttachmentType
import hela.eillance.utils.shareContent
import hela.eillance.viewmodels.ConversationViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

enum class SortMode { DATE, NAME, SIZE }

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ConversationMediaScreen(
  peerId: String,
  chatVM: ConversationViewModel,
  onBack: () -> Unit,
  onNavigateToMediaPreview: (List<Attachment>, Int) -> Unit
) {
  var attachments by remember { mutableStateOf<List<AttachmentInfo>>(emptyList()) }
  var selectedTab by remember { mutableStateOf(0) }
  var sortMode by remember { mutableStateOf(SortMode.DATE) }
  var showSortMenu by remember { mutableStateOf(false) }

  var selectedIds by remember { mutableStateOf<Set<String>>(emptySet()) }
  val isSelectionMode = selectedIds.isNotEmpty()
  val context = LocalContext.current

  val coroutineScope = rememberCoroutineScope()
  var isMoveAction by remember { mutableStateOf(false) }

  // Directory picker for multiple files export
  val exportDirLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
    if (uri != null) {
      val attachmentsToExport = attachments.filter { selectedIds.contains(it.attachment.id) }.map { it.attachment }
      coroutineScope.launch(Dispatchers.IO) {
        try {
          val documentTree = DocumentFile.fromTreeUri(context, uri)
          if (documentTree != null) {
            for (att in attachmentsToExport) {
              val file = File(att.path)
              if (file.exists()) {
                val mime = att.mime.ifEmpty { "*/*" }
                val name = att.fileName.ifEmpty { "file" }
                val newDoc = documentTree.createFile(mime, name)
                if (newDoc != null) {
                  context.contentResolver.openOutputStream(newDoc.uri)?.use { out ->
                    file.inputStream().use { input -> input.copyTo(out) }
                  }
                  if (isMoveAction) {
                    chatVM.deleteAttachmentLocal(att.id)
                  }
                }
              }
            }
            withContext(Dispatchers.Main) {
              Toast.makeText(context, "Saved", Toast.LENGTH_SHORT).show()
              if (isMoveAction) {
                attachments = attachments.filterNot { selectedIds.contains(it.attachment.id) }
              }
              selectedIds = emptySet()
            }
          }
        } catch (e: Exception) {
          withContext(Dispatchers.Main) {
            Toast.makeText(context, "Failed to save", Toast.LENGTH_SHORT).show()
          }
        }
      }
    }
  }

  LaunchedEffect(Unit) {
    attachments = chatVM.getDownloadedAttachments()
  }

  BackHandler(enabled = isSelectionMode) {
    selectedIds = emptySet()
  }

  val sortedAttachments = remember(attachments, sortMode) {
    when (sortMode) {
      SortMode.DATE -> attachments.sortedByDescending { it.timestamp }
      SortMode.NAME -> attachments.sortedBy { it.attachment.fileName.lowercase() }
      SortMode.SIZE -> attachments.sortedByDescending { it.attachment.size }
    }
  }

  val mediaItems = sortedAttachments.filter {
    it.attachment.type == AttachmentType.Image || it.attachment.type == AttachmentType.Video
  }
  val fileItems = sortedAttachments.filter {
    it.attachment.type == AttachmentType.File
  }

  val activeList = if (selectedTab == 0) mediaItems else fileItems

  Scaffold(
    topBar = {
      if (isSelectionMode) {
        var showSelectionMenu by remember { mutableStateOf(false) }
        TopAppBar(
          title = { Text(stringResource(R.string.selected_count, selectedIds.size)) },
          navigationIcon = {
            IconButton(onClick = { selectedIds = emptySet() }) {
              Icon(Icons.Default.Close, contentDescription = stringResource(R.string.action_close))
            }
          },
          actions = {
            Box {
              IconButton(onClick = { showSelectionMenu = true }) {
                Icon(Icons.Default.MoreVert, contentDescription = "Options")
              }
              DropdownMenu(expanded = showSelectionMenu, onDismissRequest = { showSelectionMenu = false }) {
                DropdownMenuItem(
                  text = { Text(stringResource(id = R.string.action_share)) },
                  onClick = {
                    showSelectionMenu = false
                    val selectedAttachments =
                      attachments.filter { selectedIds.contains(it.attachment.id) }.map { it.attachment }
                    shareContent(context, null, selectedAttachments)
                    selectedIds = emptySet()
                  }
                )
                DropdownMenuItem(
                  text = { Text(stringResource(id = R.string.action_copy_to)) },
                  onClick = {
                    showSelectionMenu = false
                    isMoveAction = false
                    exportDirLauncher.launch(null)
                  }
                )
                DropdownMenuItem(
                  text = { Text(stringResource(id = R.string.action_move_to)) },
                  onClick = {
                    showSelectionMenu = false
                    isMoveAction = true
                    exportDirLauncher.launch(null)
                  }
                )
                DropdownMenuItem(
                  text = { Text(stringResource(id = R.string.action_delete)) },
                  onClick = {
                    showSelectionMenu = false
                    selectedIds.forEach { chatVM.deleteAttachmentLocal(it) }
                    attachments = attachments.filterNot { selectedIds.contains(it.attachment.id) }
                    selectedIds = emptySet()
                  }
                )
              }
            }
          },
          colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh)
        )
      } else {
        TopAppBar(
          title = { Text(stringResource(R.string.screen_title_media)) },
          navigationIcon = {
            IconButton(onClick = onBack) {
              Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.action_back))
            }
          },
          actions = {
            Box {
              IconButton(onClick = { showSortMenu = true }) {
                Icon(Icons.AutoMirrored.Filled.Sort, contentDescription = "Sort")
              }
              DropdownMenu(expanded = showSortMenu, onDismissRequest = { showSortMenu = false }) {
                DropdownMenuItem(
                  text = { Text(stringResource(R.string.sort_date)) },
                  onClick = { sortMode = SortMode.DATE; showSortMenu = false }
                )
                DropdownMenuItem(
                  text = { Text(stringResource(R.string.sort_name)) },
                  onClick = { sortMode = SortMode.NAME; showSortMenu = false }
                )
                DropdownMenuItem(
                  text = { Text(stringResource(R.string.sort_size)) },
                  onClick = { sortMode = SortMode.SIZE; showSortMenu = false }
                )
              }
            }
          }
        )
      }
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .padding(padding)
        .fillMaxSize()
    ) {
      TabRow(selectedTabIndex = selectedTab) {
        Tab(
          selected = selectedTab == 0,
          onClick = { selectedTab = 0; selectedIds = emptySet() },
          text = { Text(stringResource(R.string.tab_media)) }
        )
        Tab(
          selected = selectedTab == 1,
          onClick = { selectedTab = 1; selectedIds = emptySet() },
          text = { Text(stringResource(R.string.tab_files)) }
        )
      }

      if (activeList.isEmpty()) {
        Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
          Text(stringResource(R.string.search_no_results))
        }
      } else {
        if (selectedTab == 0) {
          // Media Grid
          LazyVerticalGrid(
            columns = GridCells.Fixed(3),
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.spacedBy(2.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp)
          ) {
            items(mediaItems, key = { it.attachment.id }) { item ->
              val isSelected = selectedIds.contains(item.attachment.id)
              val file = File(item.attachment.path)
              val fileExists = file.exists()
              val isGif = item.attachment.mime == "image/gif" || file.extension.lowercase() == "gif"

              Box(
                modifier = Modifier
                  .aspectRatio(1f)
                  .combinedClickable(
                    onClick = {
                      if (isSelectionMode) {
                        selectedIds =
                          if (isSelected) selectedIds - item.attachment.id else selectedIds + item.attachment.id
                      } else {
                        if (fileExists) {
                          val index = mediaItems.indexOf(item)
                          onNavigateToMediaPreview(mediaItems.map { it.attachment }, index)
                        } else {
                          Toast.makeText(context, R.string.media_error_image, Toast.LENGTH_SHORT).show()
                        }
                      }
                    },
                    onLongClick = {
                      if (!isSelectionMode) {
                        selectedIds = setOf(item.attachment.id)
                      }
                    }
                  )
              ) {
                if (fileExists) {
                  if (isGif) {
                    val gifBitmap = remember(item.attachment.path) {
                      android.graphics.BitmapFactory.decodeFile(file.absolutePath)?.asImageBitmap()
                    }
                    if (gifBitmap != null) {
                      androidx.compose.foundation.Image(
                        bitmap = gifBitmap,
                        contentDescription = null,
                        modifier = Modifier.fillMaxSize(),
                        contentScale = ContentScale.Crop
                      )
                    }
                  } else {
                    val isVideo = item.attachment.type == AttachmentType.Video
                    val model = remember(item.attachment.path, isVideo) {
                      if (isVideo) {
                        coil.request.ImageRequest.Builder(context)
                          .data(file)
                          .decoderFactory { result, options, _ ->
                            coil.decode.VideoFrameDecoder(
                              result.source,
                              options
                            )
                          }
                          .videoFrameMillis(1000)
                          .build()
                      } else file
                    }
                    AsyncImage(
                      model = model,
                      contentDescription = null,
                      modifier = Modifier.fillMaxSize(),
                      contentScale = ContentScale.Crop
                    )
                  }
                } else {
                  val thumbBitmap = remember(item.attachment.thumbnail) {
                    if (!item.attachment.thumbnail.isNullOrEmpty()) {
                      try {
                        val bytes = android.util.Base64.decode(item.attachment.thumbnail, android.util.Base64.DEFAULT)
                        android.graphics.BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
                      } catch (e: Exception) {
                        null
                      }
                    } else null
                  }
                  if (thumbBitmap != null) {
                    androidx.compose.foundation.Image(
                      bitmap = thumbBitmap,
                      contentDescription = null,
                      modifier = Modifier.fillMaxSize(),
                      contentScale = ContentScale.Crop
                    )
                  } else {
                    Box(
                      modifier = Modifier
                        .fillMaxSize()
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                    )
                  }
                  Box(
                    modifier = Modifier
                      .fillMaxSize()
                      .background(Color.Black.copy(alpha = 0.6f)),
                    contentAlignment = Alignment.Center
                  ) {
                    val errorText = if (item.attachment.type == AttachmentType.Video) {
                      stringResource(R.string.media_error_video)
                    } else {
                      stringResource(R.string.media_error_image)
                    }
                    Text(
                      text = errorText,
                      color = Color.White,
                      style = MaterialTheme.typography.labelSmall,
                      textAlign = TextAlign.Center,
                      modifier = Modifier.padding(4.dp)
                    )
                  }
                }

                if (isSelected) {
                  Box(
                    modifier = Modifier
                      .fillMaxSize()
                      .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.4f))
                  )
                  Icon(
                    Icons.Default.CheckCircle,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier
                      .align(Alignment.TopEnd)
                      .padding(4.dp)
                  )
                }
              }
            }
          }
        } else {
          // Files List
          LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
          ) {
            items(fileItems, key = { it.attachment.id }) { item ->
              val isSelected = selectedIds.contains(item.attachment.id)
              Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                  .fillMaxWidth()
                  .clip(MaterialTheme.shapes.small)
                  .background(if (isSelected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface)
                  .padding(4.dp)
              ) {
                Box(modifier = Modifier.weight(1f)) {
                  FileAttachmentItem(
                    attachment = item.attachment,
                    progress = null,
                    isFromMe = false, // Not relevant here
                    selectionMode = isSelectionMode,
                    onClick = {
                      val file = File(item.attachment.path)
                      if (file.exists()) {
                        openFile(context, file, item.attachment.mime)
                      } else {
                        Toast.makeText(context, R.string.media_error_file, Toast.LENGTH_SHORT).show()
                      }
                    },
                    onLongClick = {
                      selectedIds =
                        if (isSelected) selectedIds - item.attachment.id else selectedIds + item.attachment.id
                    },
                    onCancel = {},
                    onResume = {},
                    onDiscard = {}
                  )
                }
                if (isSelected) {
                  Icon(
                    Icons.Default.CheckCircle,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.padding(start = 8.dp)
                  )
                }
              }
            }
          }
        }
      }
    }
  }
}

private fun openFile(context: Context, file: File, mimeType: String) {
  if (!file.exists()) return
  val uri = FileProvider.getUriForFile(context, "${context.packageName}.provider", file)
  val intent = Intent(Intent.ACTION_VIEW).apply {
    setDataAndType(uri, mimeType)
    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
  }
  try {
    context.startActivity(intent)
  } catch (_: ActivityNotFoundException) {
    Toast.makeText(context, context.getString(R.string.toast_no_app_for_file), Toast.LENGTH_SHORT).show()
  }
}
