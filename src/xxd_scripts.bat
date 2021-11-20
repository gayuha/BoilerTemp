@echo off
xxd -i scripts_compressed.js scripts.h
echo Converted scripts_compressed.js
echo Remember to zero terminate the string!!!
echo And remove 'unsigned'
pause