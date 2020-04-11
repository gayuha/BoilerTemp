@echo off
xxd -i index.html index.h
echo Remember to zero terminate the string!!!
echo And remove 'unsigned'
pause