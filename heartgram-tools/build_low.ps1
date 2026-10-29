# Runs the release build at below-normal priority so the PC stays usable; the compilers inherit it.
param([string]$Log = 'D:\PythonProjects\AyuGramDesktop\_toolchain\build_last.log', [switch]$Fast)
if ($Fast) { $env:HG_FAST = '1' }
$bat = 'D:\PythonProjects\AyuGramDesktop\_toolchain\build_extera_mp2.bat'
$p = Start-Process cmd.exe -ArgumentList ('/c ""{0}" > "{1}" 2>&1"' -f $bat, $Log) -PassThru -WindowStyle Hidden
$p.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
$p.WaitForExit()
exit $p.ExitCode
