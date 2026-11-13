$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$base = 'http://127.0.0.1:8080'
function Post-Json($path, $body) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes(($body | ConvertTo-Json -Compress))
    Invoke-RestMethod -Uri ($base + $path) -Method Post -Body $bytes -ContentType 'application/json; charset=utf-8'
}
try {
    $health = Invoke-RestMethod ($base + '/api/health')
    Write-Host ('Server: ' + $health.status)
    $name = Read-Host 'Platform name (example: Laptop 16.5)'
    $first = Read-Host 'First product ID (example: V00001)'
    $count = [int](Read-Host 'Number of cards')
    $preview = Post-Json '/api/batches/preview' @{ first_product_id = $first; count = $count }
    Write-Host ('Range: ' + $preview.first_product_id + ' ... ' + $preview.last_product_id)
    $answer = Read-Host 'Create these cards? Type YES to continue'
    if ($answer -ne 'YES') { Write-Host 'Cancelled'; exit 0 }
    $platform = Post-Json '/api/platforms' @{ name = $name }
    $batch = Post-Json '/api/batches' @{ platform_type_id = $platform.id; first_product_id = $first; count = $count }
    $batch.cards | Format-Table id, product_id, view_path
    Write-Host ('Next suggested ID: ' + $batch.next_product_id)
    Start-Process $base
} catch {
    Write-Host ('Error: ' + $_.Exception.Message)
    exit 1
}
