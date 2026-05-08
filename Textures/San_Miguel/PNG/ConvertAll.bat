@echo off
echo PNG to DDS 변환을 시작합니다...

:: 1. 현재 폴더의 모든 png 파일을 BC7 포맷으로 압축하고 밉맵을 생성
texconv.exe -m 0 -f BC7_UNORM *.png

:: 2. 변환된 DDS 파일들을 모아둘 새 폴더(DDS_Output) 생성
if not exist "DDS_Output" mkdir "DDS_Output"

:: 3. 생성된 모든 .dds 파일을 방금 만든 폴더로 이동
move *.dds "DDS_Output\"

echo 변환 및 이동 완료!
pause