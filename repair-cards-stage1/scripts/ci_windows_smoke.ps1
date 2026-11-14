param([string]$BuildDir)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = (Resolve-Path $BuildDir).Path
$test = Join-Path $env:TEMP ('RepairCardsSmoke-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $test | Out-Null
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$config = @{host='0.0.0.0';port=$port;database=(Join-Path $test 'smoke.sqlite3');template=(Join-Path $root 'templates/blank.xlsx');web=(Join-Path $root 'web')} | ConvertTo-Json
$configPath = Join-Path $test 'config.json'
[IO.File]::WriteAllText($configPath,$config)
$server = $null
$clients = @()
$diagnostic = Join-Path $env:LOCALAPPDATA 'RepairCards/viewer-diagnostic.json'
try {
 $server = Start-Process (Join-Path $build 'repair_server.exe') -ArgumentList @('--config',('"'+$configPath+'"')) -PassThru -RedirectStandardOutput (Join-Path $test 'server.log') -RedirectStandardError (Join-Path $test 'error.log')
 $base = "http://127.0.0.1:$port"
 $ready = $false
 for($i=0;$i -lt 100;$i++) {try {$null=Invoke-RestMethod "$base/api/health";$ready=$true;break} catch {Start-Sleep -Milliseconds 100}}
 if(!$ready){throw 'Server did not start'}
 $platform=Invoke-RestMethod "$base/api/platforms" -Method Post -ContentType 'application/json' -Body '{"name":"Windows smoke"}'
 $body=@{platform_type_id=$platform.id;first_product_id='V00001';count=10}|ConvertTo-Json
 $null=Invoke-RestMethod "$base/api/batches" -Method Post -ContentType 'application/json' -Body $body
 if(Test-Path (Join-Path $build 'client_ui_tests.exe')){& (Join-Path $build 'client_ui_tests.exe') $base;if($LASTEXITCODE -ne 0){throw 'Native Creator allocation UI tests failed'}}
 $creator=Start-Process (Join-Path $build 'Creator.exe') -PassThru
 $clients += $creator
 $title=''
 for($i=0;$i -lt 100;$i++){$creator.Refresh();$title=$creator.MainWindowTitle;if($title -eq 'Creator'){break};if($creator.HasExited){throw 'Creator exited at startup'};Start-Sleep -Milliseconds 100}
 if($title -ne 'Creator'){throw "Creator title not found: $title"}
 Write-Output 'Creator native main window opened'
 Stop-Process -Id $creator.Id -ErrorAction SilentlyContinue
 $env:REPAIRCARDS_DIAGNOSTICS='1'
 foreach($role in @('Reader','Repair')) {
  Remove-Item $diagnostic -ErrorAction SilentlyContinue
  $client=Start-Process (Join-Path $build ($role+'.exe')) -ArgumentList "$base/cards/1" -PassThru
  $clients += $client
  $result=$null
  for($i=0;$i -lt 300;$i++) {
   if(Test-Path $diagnostic){try{$result=Get-Content $diagnostic -Raw|ConvertFrom-Json}catch{};if($result){break}}
   if($client.HasExited){throw "$role exited at startup"}
   Start-Sleep -Milliseconds 100
  }
  if(!$result){throw "$role did not complete embedded page handshake"}
  if($result.event -ne 'ready'){throw "$role WebView2 failed: $($result.message)"}
  if($result.editable -ne ($role -eq 'Repair')){throw "$role has wrong document mode"}
  if($result.mode -ne $(if($role -eq 'Repair'){'edit'}else{'read'})){throw "$role has wrong page mode"}
  Write-Output "$role native embedded page ready, editable=$($result.editable), card=$($result.title)"
  Stop-Process -Id $client.Id -ErrorAction SilentlyContinue
 }
} finally {
 foreach($client in $clients){Stop-Process -Id $client.Id -ErrorAction SilentlyContinue}
 if($server){Stop-Process -Id $server.Id -ErrorAction SilentlyContinue}
 if(Test-Path $diagnostic){Copy-Item $diagnostic (Join-Path $build 'viewer-diagnostic.json') -Force}
 Get-Content (Join-Path $test 'error.log') -ErrorAction SilentlyContinue
}
