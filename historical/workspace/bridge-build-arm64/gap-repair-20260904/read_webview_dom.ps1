$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$targets = (Invoke-WebRequest -UseBasicParsing 'http://127.0.0.1:9222/json').Content | ConvertFrom-Json
$results = @()
foreach ($target in $targets) {
    if ($target.type -ne 'page') { continue }
    $client = New-Object System.Net.WebSockets.ClientWebSocket
    $cancel = New-Object System.Threading.CancellationTokenSource
    $cancel.CancelAfter(15000)
    try {
        $null = $client.ConnectAsync([Uri]$target.webSocketDebuggerUrl, $cancel.Token).GetAwaiter().GetResult()
        $expression = 'JSON.stringify({title:document.title,location:location.origin+location.pathname,ready:document.readyState,scrollY:window.scrollY,innerHeight:window.innerHeight,text:document.body?document.body.innerText.slice(0,2400):null,textLength:document.body?document.body.innerText.length:0,height:document.documentElement.scrollHeight,scrollers:Array.from(document.querySelectorAll("*")).filter(e=>e.scrollHeight>e.clientHeight+100 && e.clientHeight>0).slice(0,12).map(e=>({tag:e.tagName,id:e.id,top:e.scrollTop,height:e.clientHeight,scrollHeight:e.scrollHeight})),links:Array.from(document.querySelectorAll("a")).filter(a=>a.innerText.trim()).slice(0,8).map(a=>({text:a.innerText.slice(0,100),path:new URL(a.href,location.href).pathname}))})'
        $message = @{id=1;method='Runtime.evaluate';params=@{expression=$expression;returnByValue=$true}} | ConvertTo-Json -Depth 6 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($message)
        $null = $client.SendAsync([ArraySegment[byte]]::new($bytes), [Net.WebSockets.WebSocketMessageType]::Text, $true, $cancel.Token).GetAwaiter().GetResult()
        do {
            $stream = New-Object IO.MemoryStream
            do {
                $buffer = New-Object byte[] 8192
                $received = $client.ReceiveAsync([ArraySegment[byte]]::new($buffer), $cancel.Token).GetAwaiter().GetResult()
                $stream.Write($buffer, 0, $received.Count)
            } while (!$received.EndOfMessage)
            $response = [Text.Encoding]::UTF8.GetString($stream.ToArray()) | ConvertFrom-Json
            $stream.Dispose()
        } while ($response.id -ne 1)
        $results += @{target=$target.id;dom=($response.result.result.value | ConvertFrom-Json);exception=$response.result.exceptionDetails;error=$response.error}
    } catch {
        $results += @{target=$target.id;error=$_.Exception.Message}
    } finally {
        $client.Dispose()
        $cancel.Dispose()
    }
}
$results | ConvertTo-Json -Depth 8
