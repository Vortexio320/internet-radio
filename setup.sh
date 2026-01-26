#!/bin/bash
set -e # Zatrzymaj skrypt, jeśli wystąpi jakikolwiek błąd

echo "--- [1/5] AKTUALIZACJA SYSTEMU ---"
sudo apt update

echo "--- [2/5] INSTALACJA ZALEŻNOŚCI SYSTEMOWYCH ---"
# ffmpeg: do muzyki
# build-essential: kompilatory g++
# python3-tk: interfejs graficzny
# portaudio19-dev: potrzebne, żeby zainstalować PyAudio
sudo apt install -y ffmpeg build-essential python3-pip python3-tk portaudio19-dev

echo "--- [3/5] INSTALACJA BIBLIOTEK PYTHON ---"
pip3 install pyaudio --break-system-packages 2>/dev/null || pip3 install pyaudio

echo "--- [4/5] KOMPILACJA SERWERA ---"
cd server
if [ -f "main.cpp" ]; then
    g++ -Wall -std=c++14 -pthread main.cpp -o radio_server
    echo "✔ Serwer skompilowany pomyślnie."
else
    echo "❌ BŁĄD: Nie znaleziono main.cpp w folderze server!"
    exit 1
fi

echo "--- [5/5] SPRAWDZANIE PLIKU STARTOWEGO (elevatormusic.mp3) ---"
if [ ! -f "elevatormusic.mp3" ]; then
    echo "⚠ Nie znaleziono elevatormusic.mp3!"
    echo "Generating dummy silence mp3 to prevent server crash..."
    # Generuje 5 sekund ciszy w formacie MP3, żeby serwer miał co grać
    ffmpeg -f lavfi -i anullsrc=r=44100:cl=stereo -t 5 -q:a 9 elevatormusic.mp3 -y -hide_banner -loglevel error
    echo "✔ Utworzono tymczasowy plik elevatormusic.mp3 (cisza)."
else
    echo "✔ Plik elevatormusic.mp3 istnieje."
fi

cd ..
echo ""
echo "========================================"
echo "   GOTOWE! ŚRODOWISKO ZAINSTALOWANE.   "
echo "========================================"
echo "Jak uruchomić:"
echo "1. Terminal 1: cd server && ./radio_server"
echo "2. Terminal 2: cd client && python3 main.py"
