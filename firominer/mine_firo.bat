rem firominer.exe -P "stratum+tcp://a7GJ9bYfbZqvXmEmxzxZyZYtUTQmzckpjG.worker1:x@firo.cedric-crispin.com:4064"
@echo off
rem Replace WALLET, WORKER and PASSWORD below with your pool login details.
"%~dp0firominer.exe" -P "stratum+tcp://WALLET.WORKER:PASSWORD@firo.cedric-crispin.com:4064"
pause
