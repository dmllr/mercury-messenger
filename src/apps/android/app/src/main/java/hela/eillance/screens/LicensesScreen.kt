package hela.eillance.screens

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.theme.ListItem

private data class LicenseItem(
  val name: String,
  val licenseType: String,
  val licenseText: String
)

private val apache2Text = """
    |Copyright (c) The respective authors and contributors.
    |
    |Licensed under the Apache License, Version 2.0 (the "License");
    |you may not use this file except in compliance with the License.
    |You may obtain a copy of the License at
    |
    |    http://www.apache.org/licenses/LICENSE-2.0
    |
    |Unless required by applicable law or agreed to in writing, software
    |distributed under the License is distributed on an "AS IS" BASIS,
    |WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    |See the License for the specific language governing permissions and
    |limitations under the License.
    |
    |No modifications were made to the original source files of this library.
""".trimMargin()

private val mitText = """
    |Copyright (c) The respective authors and contributors.
    |
    |Permission is hereby granted, free of charge, to any person obtaining a copy
    |of this software and associated documentation files (the "Software"), to deal
    |in the Software without restriction, including without limitation the rights
    |to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    |copies of the Software, and to permit persons to whom the Software is
    |furnished to do so, subject to the following conditions:
    |
    |The above copyright notice and this permission notice shall be included in all
    |copies or substantial portions of the Software.
    |
    |THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    |IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    |FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    |AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    |LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    |OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    |SOFTWARE.
""".trimMargin()

private val bsd3Text = """
    |Copyright (c) The respective authors and contributors.
    |
    |Redistribution and use in source and binary forms, with or without
    |modification, are permitted provided that the following conditions are met:
    |
    |1. Redistributions of source code must retain the above copyright notice, this
    |   list of conditions and the following disclaimer.
    |
    |2. Redistributions in binary form must reproduce the above copyright notice,
    |   this list of conditions and the following disclaimer in the documentation
    |   and/or other materials provided with the distribution.
    |
    |3. Neither the name of the copyright holder nor the names of its
    |   contributors may be used to endorse or promote products derived from
    |   this software without specific prior written permission.
    |
    |THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    |AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    |IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    |DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    |FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    |DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    |SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    |CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    |OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    |OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
""".trimMargin()

private val licenses = listOf(
  LicenseItem("AndroidX & Jetpack Compose", "Apache 2.0", apache2Text),
  LicenseItem("Google WebRTC", "BSD 3-Clause", bsd3Text),
  LicenseItem("OpenCV", "Apache 2.0", apache2Text),
  LicenseItem("Rust libp2p Ecosystem", "MIT / Apache 2.0", "Dual-licensed under MIT and Apache 2.0.\n\n$apache2Text\n\n$mitText"),
  LicenseItem("Tokio Ecosystem", "MIT", mitText),
  LicenseItem("BoringSSL", "OpenSSL / ISC", "Licensed under the OpenSSL License and ISC License."),
  LicenseItem("Coil", "Apache 2.0", apache2Text),
  LicenseItem("OkHttp", "Apache 2.0", apache2Text),
  LicenseItem("ZXing Core", "Apache 2.0", apache2Text),
  LicenseItem("nlohmann/json", "MIT", mitText),
  LicenseItem("miniaudio", "MIT", mitText),
  LicenseItem("libsrtp", "BSD 3-Clause", bsd3Text),
  LicenseItem("libusrsctp", "BSD 2-Clause", bsd3Text)
).sortedBy { it.name }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LicensesScreen(onBack: () -> Unit) {
  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_licenses)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_back)
            )
          }
        }
      )
    }
  ) { padding ->
    LazyColumn(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
    ) {
      items(licenses) { item ->
        LicenseRow(item)
      }
    }
  }
}

@Composable
private fun LicenseRow(item: LicenseItem) {
  var expanded by remember { mutableStateOf(false) }

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .clickable { expanded = !expanded }
  ) {
    ListItem(
      headlineContent = { Text(item.name) },
      supportingContent = { Text(item.licenseType) },
      trailingContent = {
        Icon(
          imageVector = if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
          contentDescription = null
        )
      }
    )
    AnimatedVisibility(visible = expanded) {
      Text(
        text = item.licenseText,
        style = MaterialTheme.typography.bodySmall,
        fontFamily = FontFamily.Monospace,
        modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp)
      )
    }
  }
}
