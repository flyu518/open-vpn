param(
    [string]$OpenVpnDirectory = 'tmp\openvpn-official-server',
    [string]$FixtureDirectory = 'tmp\openvpn-core-fixture',
    [string]$OpenSsl = 'C:\msys64\mingw64\bin\openssl.exe'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$serverDirectory=(Resolve-Path -LiteralPath (Join-Path $root $OpenVpnDirectory)).Path
$fixture=Join-Path $root $FixtureDirectory
$fixture=[IO.Path]::GetFullPath($fixture)
if(-not $fixture.StartsWith((Join-Path $root 'tmp\'),[StringComparison]::OrdinalIgnoreCase)){
    throw 'Core fixtures must stay inside the workspace tmp directory'
}
$processFile=Join-Path $fixture 'server-processes.json'
if(Test-Path -LiteralPath $processFile){
    foreach($record in (Get-Content -LiteralPath $processFile -Raw|ConvertFrom-Json)){
        $previous=Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
        if($previous){
            if(-not $record.created -or $previous.ExecutablePath -ne (Join-Path $serverDirectory 'bin.openvpn.exe') -or
               $previous.CommandLine -notlike "*--config $($record.config)*"){
                throw 'Fixture PID was reused; refusing to stop an unrelated process'
            }
            $verified=Get-Process -Id $record.pid -ErrorAction Stop
            $null=$verified.Handle
            if($verified.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -ne $record.created){
                throw 'Fixture process changed during verification'
            }
            $verified.Kill()
        }
    }
}
New-Item -ItemType Directory -Path (Join-Path $fixture 'profiles') -Force|Out-Null
foreach($pair in @(@('libcrypto_3_x64.dll','libcrypto-3-x64.dll'),@('libssl_3_x64.dll','libssl-3-x64.dll'),@('libpkcs11_helper_1.dll','libpkcs11-helper-1.dll'))){
    Copy-Item -LiteralPath (Join-Path $serverDirectory $pair[0]) -Destination (Join-Path $serverDirectory $pair[1]) -Force
}
$server=Join-Path $serverDirectory 'bin.openvpn.exe'
$signature=Get-AuthenticodeSignature -LiteralPath $server
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'OpenVPN Inc'){
    throw 'Official OpenVPN server signature did not validate'
}
function WriteFixture([string]$Name,[string]$Contents){
    [IO.File]::WriteAllText((Join-Path $fixture $Name),$Contents,[Text.UTF8Encoding]::new($false))
}
function OpenSslCommand([string[]]$Arguments){
    & $OpenSsl @Arguments 2>$null
    if($LASTEXITCODE -ne 0){throw "OpenSSL failed: $($Arguments[0])"}
}
WriteFixture 'openssl.cnf' "[req]`ndistinguished_name=dn`n[dn]`n"
WriteFixture 'server.ext' "basicConstraints=CA:FALSE`nkeyUsage=digitalSignature,keyEncipherment`nextendedKeyUsage=serverAuth`n"
WriteFixture 'client.ext' "basicConstraints=CA:FALSE`nkeyUsage=digitalSignature`nextendedKeyUsage=clientAuth`n"
Push-Location $fixture
try {
    OpenSslCommand @('req','-new','-x509','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256','-nodes','-keyout','ca.key','-out','ca.crt','-days','2','-subj','/CN=OpenVPNCoreFixtureCA','-config','openssl.cnf','-addext','basicConstraints=critical,CA:TRUE','-addext','keyUsage=critical,keyCertSign,cRLSign')
    foreach($name in @('server','client')){
        OpenSslCommand @('req','-new','-newkey','ec','-pkeyopt','ec_paramgen_curve:P-256','-nodes','-keyout',"$name.key",'-out',"$name.csr",'-subj',"/CN=OpenVPNCoreFixture-$name",'-config','openssl.cnf')
        OpenSslCommand @('x509','-req','-in',"$name.csr",'-CA','ca.crt','-CAkey','ca.key','-set_serial',$(if($name -eq 'server'){'2'}else{'3'}),'-out',"$name.crt",'-days','2','-extfile',"$name.ext")
    }
}finally{Pop-Location}
$profiles=@()
$processes=@()
for($i=0;$i -lt 2;$i++){
    $port=21194+$i
    $network=251+$i
    $config=@"
dev null
proto tcp-server
local 127.0.0.1
port $port
tls-server
ca ca.crt
cert server.crt
key server.key
dh none
disable-dco
data-ciphers AES-256-GCM
cipher AES-256-GCM
push "topology subnet"
push "ifconfig 10.253.$network.2 255.255.255.0"
push "route-gateway 10.253.$network.1"
push "route 198.18.20.0 255.255.255.0"
push "dhcp-option DNS 192.0.2.53"
push "dhcp-option DOMAIN fixture-$i.invalid"
push "block-ipv6"
$(if($i -eq 1){'push "route 198.18.21.0 255.255.255.0"'})
keepalive 5 30
verb 3
log server-$i.log
"@
    WriteFixture "server-$i.ovpn" $config
    $client=@"
client
dev tun
proto tcp-client
remote 127.0.0.1 $port
nobind
remote-cert-tls server
data-ciphers AES-256-GCM
cipher AES-256-GCM
verb 3
<ca>
$([IO.File]::ReadAllText((Join-Path $fixture 'ca.crt')))
</ca>
<cert>
$([IO.File]::ReadAllText((Join-Path $fixture 'client.crt')))
</cert>
<key>
$([IO.File]::ReadAllText((Join-Path $fixture 'client.key')))
</key>
"@
    WriteFixture "profiles\client-$i.ovpn" $client
    $profiles+=@{id="fixture-$i";name="Local OpenVPN Fixture $i";managedFile="client-$i.ovpn";path=(Join-Path $fixture "profiles\client-$i.ovpn");routeStrategy=@{conflictMode='compatible';rules=@()}}
    $process=Start-Process -FilePath $server -ArgumentList '--config',"server-$i.ovpn" -WorkingDirectory $fixture -WindowStyle Hidden -PassThru
    $processes+=@{pid=$process.Id;path=$server;config="server-$i.ovpn";created=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
}
WriteFixture 'profiles.json' (@{version=1;profiles=$profiles}|ConvertTo-Json -Depth 8)
WriteFixture 'server-processes.json' ($processes|ConvertTo-Json)
Start-Sleep -Milliseconds 500
foreach($process in $processes){
    if(-not(Get-Process -Id $process.pid -ErrorAction SilentlyContinue)){throw 'Fixture server exited; inspect server log'}
}
Write-Output "FIXTURE_READY=$fixture"
