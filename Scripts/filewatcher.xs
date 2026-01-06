' FileWatcherDemo.cb
'
' Demo CrossBasic script for FileWatcher.dll (FileWatcher plugin)
' - Watches a directory
' - Prints Created / Changed / Deleted events
' - Runs until user presses Enter
'
' Event payload format from plugin:
'   "filename|fullpath"
'
' IMPORTANT:
' - FileWatcher uses a background thread (polling).
' - Set Directory, Filter, IncludeSubdirs, PollIntervalMs, then Enabled=True (or call Start()).

' -----------------------------
' Event callbacks
' -----------------------------
Sub OnCreated(payload As CString)
  Print("[Created] " + payload)
End Sub

Sub OnChanged(payload As CString)
  Print("[Changed] " + payload)
End Sub

Sub OnDeleted(payload As CString)
  Print("[Deleted] " + payload)
End Sub

Sub OnError(payload As CString)
  Print("[Error] " + payload)
End Sub

' -----------------------------
' Main
' -----------------------------
Sub Main()
  Dim w As New FileWatcher

  ' Register callbacks (event names must match plugin: "Created", "Changed", "Deleted", "Error")
  Call w.SetEventCallback("Created", AddressOf(OnCreated))
  Call w.SetEventCallback("Changed", AddressOf(OnChanged))
  Call w.SetEventCallback("Deleted", AddressOf(OnDeleted))
  Call w.SetEventCallback("Error",   AddressOf(OnError))

  ' Configure watcher
  w.Directory = "."        ' current directory (use absolute path if you prefer)
  w.Filter = "*"           ' examples: "*", "*.txt", "data*.json"
  w.IncludeSubdirs = False ' set True to recurse
  w.PollIntervalMs = 500   ' polling interval (ms)

  ' Start watching
  w.Enabled = True         ' or: w.Start()

  Print("Watching: " + w.Directory)
  Print("Filter:   " + w.Filter)
  Print("Recurse:  " + (If(w.IncludeSubdirs, "true", "false")))
  Print("Poll ms:  " + w.PollIntervalMs.ToString)
  Print("")
  Print("Now create/edit/delete files in that directory to see events.")
  Print("Press Enter to stop...")

  ' Wait for user input (keeps process alive so background thread can run)
  Dim s As String
  s = Input()

  ' Stop watching and cleanup
  w.Enabled = False        ' or: w.Stop()
  w.Close

  Print("Stopped.")
End Sub

Main()
