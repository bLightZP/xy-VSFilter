param(
    [Parameter(Mandatory=$true)][string]$Folder,
    [Parameter(Mandatory=$true)][string]$Symbols,
    [string]$VisualStudioRoot = 'C:\Program Files\Microsoft Visual Studio\18\Community',
    [double]$StartElapsed = 0,
    [double]$EndElapsed = [double]::PositiveInfinity
)
# Uses libraries already supplied with Visual Studio; no package downloads.
$ErrorActionPreference = 'Stop'
$Folder = (Resolve-Path $Folder).Path
$Symbols = (Resolve-Path $Symbols).Path
Add-Type -Path "$VisualStudioRoot\Common7\IDE\CommonExtensions\Microsoft\TestWindow\VsTest\Extensions\Microsoft.Diagnostics.NETCore.Client.dll"
Add-Type -Path "$VisualStudioRoot\Common7\IDE\PrivateAssemblies\Microsoft.Diagnostics.Tracing.TraceEvent.dll"
$etl = Get-ChildItem $Folder -Recurse -Filter sc.user_aux.etl | Select-Object -First 1
if (!$etl) {
    [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $Folder 'cpu.diagsession'), (Join-Path $Folder 'trace'))
    $etl = Get-ChildItem $Folder -Recurse -Filter sc.user_aux.etl | Select-Object -First 1
}
$etlx = Join-Path $Folder 'cpu.etlx'
if (!(Test-Path $etlx)) {
    [Microsoft.Diagnostics.Tracing.Etlx.TraceLog]::CreateFromEventTraceLogFile($etl.FullName, $etlx, $null, [Microsoft.Diagnostics.Tracing.TraceEventDispatcherOptions]::None) | Out-Null
}
$log = [Microsoft.Diagnostics.Tracing.Etlx.TraceLog]::new($etlx)
$symbolLog = [IO.StreamWriter]::new((Join-Path $Folder 'symbols.log'))
$reader = [Microsoft.Diagnostics.Symbols.SymbolReader]::new($symbolLog, $Symbols, $null)
$reader.SecurityCheck = [Func[string,bool]]{ param($path) $path.StartsWith($Symbols, [StringComparison]::OrdinalIgnoreCase) }
foreach ($module in $log.ModuleFiles) {
    if ($module.Name -eq 'xysubfilter' -and $module.FilePath.StartsWith($Symbols, [StringComparison]::OrdinalIgnoreCase)) {
        $log.CodeAddresses.LookupSymbolsForModule($reader, $module)
    }
}
$metadata = Get-Content (Join-Path $Folder 'metadata.json') -Raw | ConvertFrom-Json
$rows = Import-Csv (Join-Path $Folder 'renderer.csv')
$captureStart = [datetimeoffset]::Parse($rows[0].utc).LocalDateTime
$start = $captureStart.AddSeconds($StartElapsed)
$end = [datetimeoffset]::Parse($rows[-1].utc).LocalDateTime
if (![double]::IsPositiveInfinity($EndElapsed)) {
    $limit = $captureStart.AddSeconds($EndElapsed)
    if ($limit -lt $end) { $end = $limit }
}
$exclusive = @{}
$inclusive = @{}
$modules = @{}
$modulePaths = @{}
$stackSamples = @{}
$samples = 0
$stacks = 0
function FrameName($address) {
    if ($address.FullMethodName) { return "$($address.ModuleName)!$($address.FullMethodName)" }
    return ('{0}!0x{1:X}' -f $address.ModuleName, ($address.Address - $address.ModuleFile.ImageBase))
}
foreach ($ev in $log.Events) {
    if ($ev.ProcessID -ne $metadata.player_pid -or $ev.EventName -ne 'PerfInfo/Sample' -or $ev.TimeStamp -lt $start -or $ev.TimeStamp -gt $end) { continue }
    ++$samples
    $address = [Microsoft.Diagnostics.Tracing.Etlx.TraceLogExtensions]::IntructionPointerCodeAddress($ev)
    $name = FrameName $address
    $exclusive[$name]++
    $modules[[string]$address.ModuleName]++
    $modulePaths[[string]$address.ModuleFilePath]++
    $stack = [Microsoft.Diagnostics.Tracing.Etlx.TraceLogExtensions]::CallStack($ev)
    if ($null -ne $stack) { ++$stacks }
    $seen = @{}
    $seen[$name] = $true
    $chain = [Collections.Generic.List[string]]::new()
    $chain.Add($name)
    while ($null -ne $stack) {
        $frameName = FrameName $stack.CodeAddress
        $seen[$frameName] = $true
        if ($chain[$chain.Count-1] -ne $frameName) { $chain.Add($frameName) }
        $stack = $stack.Caller
    }
    foreach ($name in $seen.Keys) { $inclusive[$name]++ }
    $stackSamples[($chain -join " <- ")]++
}
$functions = foreach ($name in $inclusive.Keys) {
    [pscustomobject]@{function=$name; exclusive_samples=[int]$exclusive[$name]; inclusive_samples=[int]$inclusive[$name];
        exclusive_percent=[math]::Round(100.0 * $exclusive[$name] / $samples, 3);
        inclusive_percent=[math]::Round(100.0 * $inclusive[$name] / $samples, 3)}
}
$stackSamples.GetEnumerator() | ForEach-Object { [pscustomobject]@{stack=$_.Key; samples=$_.Value; percent=100.0*$_.Value/$samples} } |
    Sort-Object samples -Descending | Export-Csv (Join-Path $Folder 'cpu-stacks.csv') -NoTypeInformation
$functions | Sort-Object exclusive_samples -Descending | Export-Csv (Join-Path $Folder 'cpu-functions.csv') -NoTypeInformation
$modules.GetEnumerator() | ForEach-Object { [pscustomobject]@{module=$_.Key; samples=$_.Value; percent=100.0*$_.Value/$samples} } |
    Sort-Object samples -Descending | Export-Csv (Join-Path $Folder 'cpu-modules.csv') -NoTypeInformation
$modulePaths.GetEnumerator() | ForEach-Object { [pscustomobject]@{path=$_.Key; samples=$_.Value; percent=100.0*$_.Value/$samples} } |
    Sort-Object samples -Descending | Export-Csv (Join-Path $Folder 'cpu-module-paths.csv') -NoTypeInformation
$loadedXy = @($log.Processes | Where-Object ProcessID -eq $metadata.player_pid |
    ForEach-Object { $_.LoadedModules } | Where-Object Name -eq 'xysubfilter' |
    ForEach-Object { [pscustomobject]@{path=$_.ModuleFile.FilePath; pdb_guid=$_.ModuleFile.PdbSignature; pdb_age=$_.ModuleFile.PdbAge} })
[pscustomobject]@{samples=$samples; with_stacks=$stacks; events_lost=$log.EventsLost; trace_start=$log.SessionStartTime; trace_end=$log.SessionEndTime; capture_start=$start; capture_end=$end; loaded_xy_modules=$loadedXy} |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Folder 'cpu-analysis.json')
$functions | Where-Object { $_.function.StartsWith('xysubfilter!') } | Sort-Object exclusive_samples -Descending |
    Select-Object -First 20 exclusive_samples,inclusive_samples,@{n='function';e={$_.function.Substring(0,[math]::Min(145,$_.function.Length))}} | Format-Table -AutoSize
$reader.Dispose()
$symbolLog.Dispose()
$log.Dispose()
