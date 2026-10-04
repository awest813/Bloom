param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$Command
)
$ErrorActionPreference = 'Stop'
if ($Command -notmatch '^\d+ (\d+ \d+|save 9|load 9)$') {
    throw 'Expected sequence, button mask, hold duration (or save/load 9)'
}
$inputPath = [IO.Path]::GetFullPath($Path)
$temporaryPath = $inputPath + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
[IO.File]::WriteAllText($temporaryPath, $Command + "`n")
# Publish a complete line. A direct truncate/write can collide with the Lua
# reader or let it acknowledge a partially written duration.
for ($attempt = 0; $attempt -lt 40; $attempt++) {
    try {
        [IO.File]::Move($temporaryPath, $inputPath, $true)
        return
    } catch [IO.IOException], [UnauthorizedAccessException] {
        # Windows can report a reader's short-lived sharing conflict as
        # access denied. Retry only this bounded publication operation.
        if ($attempt -eq 39) { throw }
        Start-Sleep -Milliseconds 25
    }
}
