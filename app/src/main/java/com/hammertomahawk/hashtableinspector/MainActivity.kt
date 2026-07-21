package com.hammertomahawk.hashtableinspector

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp

class MainActivity : ComponentActivity() {
    private val inspectorViewModel: InspectorViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            val colorScheme = if (isSystemInDarkTheme()) {
                darkColorScheme()
            } else {
                lightColorScheme()
            }
            MaterialTheme(colorScheme = colorScheme) {
                Surface(modifier = Modifier.fillMaxSize()) {
                    InspectorScreen(inspectorViewModel)
                }
            }
        }
    }
}

@Composable
private fun InspectorScreen(viewModel: InspectorViewModel) {
    var key by rememberSaveable { mutableStateOf("a") }
    var value by rememberSaveable { mutableStateOf("1") }
    val selected = viewModel.selectedFrame

    LazyColumn(
        modifier = Modifier
            .windowInsetsPadding(WindowInsets.safeDrawing)
            .fillMaxSize()
            .clipToBounds(),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        item {
            Text(
                text = "Hash Table Inspector",
                style = MaterialTheme.typography.headlineMedium,
                fontWeight = FontWeight.Bold,
            )
            Text("Real C11 table · arm64-v8a · copied JNI snapshots")
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedTextField(
                    value = key,
                    onValueChange = { if (it.length <= 32) key = it },
                    modifier = Modifier.weight(1f),
                    label = { Text("Key") },
                    supportingText = { Text("1-32: A-Z a-z 0-9 . _ -") },
                    singleLine = true,
                )
                OutlinedTextField(
                    value = value,
                    onValueChange = { value = it },
                    modifier = Modifier.weight(1f),
                    label = { Text("int64 value") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    singleLine = true,
                )
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    modifier = Modifier.weight(1f),
                    onClick = {
                        val parsed = value.toLongOrNull()
                        if (parsed == null) {
                            viewModel.reportInputError("Value must be a signed 64-bit integer")
                        } else {
                            viewModel.put(key, parsed)
                        }
                    },
                ) { Text("Put") }
                Button(
                    modifier = Modifier.weight(1f),
                    onClick = { viewModel.lookup(key) },
                ) { Text("Lookup") }
                Button(
                    modifier = Modifier.weight(1f),
                    onClick = { viewModel.delete(key) },
                ) { Text("Delete") }
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedButton(
                    modifier = Modifier.weight(1f),
                    onClick = viewModel::validate,
                ) { Text("Validate") }
                OutlinedButton(
                    modifier = Modifier.weight(1f),
                    onClick = viewModel::corruptSize,
                    enabled = !viewModel.quarantined,
                ) { Text("Corrupt") }
                OutlinedButton(
                    modifier = Modifier.weight(1f),
                    onClick = viewModel::restore,
                    enabled = viewModel.quarantined,
                ) { Text("Restore") }
            }
        }

        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    modifier = Modifier.weight(1f),
                    onClick = viewModel::runDeterministicDemo,
                ) { Text("Run deterministic demo") }
                OutlinedButton(onClick = viewModel::reset) { Text("Reset") }
            }
        }

        item {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text("Outcome", fontWeight = FontWeight.Bold)
                    Text(viewModel.outcome)
                    Text(if (viewModel.quarantined) "Session: QUARANTINED" else "Session: operational")
                }
            }
        }

        if (selected != null) {
            item {
                SnapshotSummary(
                    frame = selected,
                    isLive = viewModel.viewingLive,
                    onLive = viewModel::selectLive,
                )
            }
            item {
                Text("Buckets and chains", style = MaterialTheme.typography.titleLarge)
            }
            items(
                items = selected.snapshot.buckets,
                key = { bucket -> "bucket-${selected.frameId}-${bucket.index}" },
            ) { bucket ->
                BucketCard(bucket)
            }
        }

        item {
            Text(
                "Timeline · newest first · ${viewModel.frames.size}/64 frames",
                style = MaterialTheme.typography.titleLarge,
            )
        }
        items(
            items = viewModel.frames.asReversed(),
            key = { frame -> frame.frameId },
        ) { frame ->
            TimelineCard(
                frame = frame,
                selected = frame.frameId == viewModel.selectedFrameId,
                onClick = { viewModel.selectFrame(frame.frameId) },
            )
        }
    }
}

@Composable
private fun SnapshotSummary(
    frame: InspectionFrame,
    isLive: Boolean,
    onLive: () -> Unit,
) {
    val snapshot = frame.snapshot
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = if (isLive) {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                MaterialTheme.colorScheme.tertiaryContainer
            },
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                if (isLive) "LIVE frame ${frame.frameId}" else "HISTORICAL frame ${frame.frameId}",
                fontWeight = FontWeight.Bold,
            )
            if (!isLive) {
                Button(
                    onClick = onLive,
                    modifier = Modifier
                        .padding(top = 8.dp)
                        .fillMaxWidth(),
                ) {
                    Text("Return to live")
                }
            }
            Text("Event: ${frame.event} · operation ${frame.operationId}")
            Text(
                "Size ${snapshot.reportedSize} (counted ${snapshot.entryCount}) · " +
                    "capacity ${snapshot.capacity} · resize at ${snapshot.resizeAt}",
            )
            Text("Load ${(snapshot.loadFactor * 100.0).toInt()}% · revision ${snapshot.revision}")
            Text(
                if (snapshot.valid) {
                    "Invariants: VALID"
                } else {
                    "Invariants: INVALID · ${snapshot.violation} (${snapshot.violationCount})"
                },
                fontWeight = FontWeight.Bold,
            )
            Text(eventDetails(frame))
        }
    }
}

@Composable
private fun BucketCard(bucket: HashBucket) {
    val collision = bucket.entries.size > 1
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                "Bucket ${bucket.index} · chain ${bucket.entries.size}" +
                    if (collision) " · COLLISION" else "",
                fontWeight = FontWeight.Bold,
            )
            if (bucket.entries.isEmpty()) {
                Text("empty")
            } else {
                bucket.entries.forEach { entry ->
                    Text("[${entry.chainIndex}] #${entry.id} ${entry.key} = ${entry.value}")
                    Text("hash ${entry.hash}", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
private fun TimelineCard(
    frame: InspectionFrame,
    selected: Boolean,
    onClick: () -> Unit,
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
        colors = CardDefaults.cardColors(
            containerColor = if (selected) {
                MaterialTheme.colorScheme.secondaryContainer
            } else {
                MaterialTheme.colorScheme.surfaceVariant
            },
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(
                "#${frame.frameId} ${frame.event}",
                fontWeight = FontWeight.Bold,
            )
            Text("operation ${frame.operationId} · ${eventDetails(frame)}")
            Text(
                "size ${frame.snapshot.reportedSize}/${frame.snapshot.entryCount} · " +
                    "capacity ${frame.snapshot.capacity} · " +
                    if (frame.snapshot.valid) "valid" else "INVALID ${frame.snapshot.violation}",
            )
        }
    }
}

private fun eventDetails(frame: InspectionFrame): String =
    if (frame.event.startsWith("rehash_")) {
        "rehash ${frame.data[0]}→${frame.data[1]}, moved=${frame.data[2]}"
    } else {
        "bucket=${frame.data[0]}, probes=${frame.data[1]}, subject=${frame.subjectId}"
    }
