@echo off
xxd -i compressed.html index.h
echo Converted index.html
echo Remember to zero terminate the string!!!
echo And remove 'unsigned'
pause