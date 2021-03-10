@echo off
xxd -i compressed.html index.h
echo Remember to zero terminate the string!!!
echo And remove 'unsigned'
pause