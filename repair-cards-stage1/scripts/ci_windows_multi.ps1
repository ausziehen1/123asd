param([string]$BuildDir,[string]$ClientDir,[switch]$Portable)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=(Resolve-Path $BuildDir).Path
$source=(Resolve-Path $ClientDir).Path
$test=Join-Path $env:TEMP ('RepairCardsMulti-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $test|Out-Null
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
$config=@{host='127.0.0.1';port=$port;database=(Join-Path $test 'multi.sqlite3');template=(Join-Path $root 'templates/blank.xlsx');web=(Join-Path $root 'web')}|ConvertTo-Json
$configPath=Join-Path $test 'config.json';[IO.File]::WriteAllText($configPath,$config)
$env:REPAIRCARDS_DIAGNOSTICS='1'
$processes=@();$instances=@();$server=$null
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class MultiUI {
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr w,string text);
 [DllImport("user32.dll",EntryPoint="SendMessageW")] public static extern IntPtr SendNumber(IntPtr window,uint message,IntPtr w,IntPtr l);
}
'@
try {
 $server=Start-Process (Join-Path $build 'repair_server.exe') -ArgumentList @('--config',('"'+$configPath+'"')) -PassThru -RedirectStandardOutput (Join-Path $test 'server.log') -RedirectStandardError (Join-Path $test 'error.log')
 $base="http://127.0.0.1:$port";$ready=$false
 for($i=0;$i -lt 100;$i++){try{$null=Invoke-RestMethod "$base/api/health";$ready=$true;break}catch{Start-Sleep -Milliseconds 100}}
 if(!$ready){throw 'Multi server did not start'}
 $platform=Invoke-RestMethod "$base/api/platforms" -Method Post -ContentType 'application/json' -Body '{"name":"Native multi"}'
 $null=Invoke-RestMethod "$base/api/batches" -Method Post -ContentType 'application/json' -Body (@{platform_type_id=$platform.id;first_product_id='V00001';count=10}|ConvertTo-Json)
 # Start every wrapper first: two simultaneous first extractions per role.
 foreach($role in @('Creator','Creator','Reader','Reader','Repair','Repair')) {
  $arguments=if($role -eq 'Creator'){@()}else{@("$base/cards/1")}
  if($role -eq 'Creator'){$p=Start-Process (Join-Path $source ($role+'.exe')) -PassThru}else{$p=Start-Process (Join-Path $source ($role+'.exe')) -ArgumentList $arguments -PassThru}
  $processes+=,$p;$instances+=,@{role=$role;wrapper=$p;client=$null}
 }
 foreach($instance in $instances) {
  $p=$instance.wrapper
  if($Portable){
   $child=$null
   for($i=0;$i -lt 200;$i++) {
    $child=Get-CimInstance Win32_Process -Filter "ParentProcessId = $($p.Id)"|Where-Object {$_.Name -eq ($instance.role+'.exe')}|Select-Object -First 1
    if($child){break};$p.Refresh();if($p.HasExited){throw 'Portable wrapper exited before starting its client'};Start-Sleep -Milliseconds 100
   }
   if(!$child){throw 'Portable client child missing'}
   $p=Get-Process -Id $child.ProcessId;$processes+=,$p
   if(!(Test-Path (Join-Path (Split-Path $child.ExecutablePath -Parent) '.portable-ready.json'))){throw 'Atomic portable extraction marker missing'}
  }
  $instance.client=$p
  for($i=0;$i -lt 200;$i++){$p.Refresh();if($p.MainWindowHandle -ne 0){break};if($p.HasExited){throw 'Native client exited'};Start-Sleep -Milliseconds 100}
  if($p.MainWindowHandle -eq 0){throw 'Native client window missing'}
  if($instance.role -eq 'Creator') {
   if($p.MainWindowTitle -ne 'Creator'){throw 'Creator window title differs'}
   $null=[MultiUI]::SendMessage([MultiUI]::GetDlgItem($p.MainWindowHandle,100),12,[IntPtr]::Zero,$base)
   $null=[MultiUI]::SendNumber([MultiUI]::GetDlgItem($p.MainWindowHandle,101),245,[IntPtr]::Zero,[IntPtr]::Zero)
   $count=0
   for($i=0;$i -lt 100;$i++){$count=[MultiUI]::SendNumber([MultiUI]::GetDlgItem($p.MainWindowHandle,108),4100,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64();if($count -eq 10){break};Start-Sleep -Milliseconds 100}
   if($count -ne 10){throw 'Concurrent Creator did not load shared cards'}
  } else {
   $diagnostic=Join-Path $env:LOCALAPPDATA ("RepairCards/viewer-"+$p.Id+'.json');$result=$null
   for($i=0;$i -lt 300;$i++){if(Test-Path $diagnostic){try{$result=Get-Content $diagnostic -Raw|ConvertFrom-Json}catch{};if($result){break}};Start-Sleep -Milliseconds 100}
   if(!$result -or $result.event -ne 'ready' -or $result.editable -ne ($instance.role -eq 'Repair')){throw "Concurrent $($instance.role) WebView2 did not open correctly"}
   if($result.pid -ne $p.Id){throw 'Diagnostic crossed client instances'}
  }
 }
 foreach($instance in $instances){$instance.client.Refresh();if($instance.client.HasExited){throw 'One of the concurrent clients closed unexpectedly'}}
 Write-Output "Concurrent native clients passed (portable=$Portable): 2 Creators connected to shared cards, 2 Readers and 2 Repairs opened the same form with isolated WebView2 profiles"
} finally {
 foreach($p in $processes){Stop-Process -Id $p.Id -ErrorAction SilentlyContinue}
 if($server){Stop-Process -Id $server.Id -ErrorAction SilentlyContinue}
 Get-Content (Join-Path $test 'error.log') -ErrorAction SilentlyContinue
}
