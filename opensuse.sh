#!/bin/bash
set -e # Zatrzymaj skrypt w przypadku błędu

echo "--- [1/5] ODŚWIEŻANIE REPOZYTORIÓW (ZYPPER) ---"
sudo zypper refresh

echo "--- [2/5] INSTALACJA ZALEŻNOŚCI SYSTEMOWYCH ---"
# gcc-c++: odpowiednik g++
# portaudio-devel: odpowiednik portaudio19-dev (dla PyAudio)
# python3-tk: interfejs graficzny
# python3-devel: nagłówki potrzebne do kompilacji niektórych bibliotek pip
sudo zypper install -y gcc-c++ python3-pip python3-tk portaudio-devel python3-devel ffmpeg

echo "--- [3/5] INSTALACJA BIBLIOTEK PYTHON ---"
# OpenSUSE też może blokować pip, więc używamy break-system-packages
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
    echo "Generowanie 5 sekund ciszy, aby serwer nie wpadł w pętlę..."
    # Generuje 5 sekund ciszy w formacie MP3
    ffmpeg -f lavfi -i anullsrc=r=44100:cl=stereo -t 5 -q:a 9 elevatormusic.mp3 -y -hide_banner -loglevel error
    echo "✔ Utworzono plik elevatormusic.mp3 (cisza)."
else
    echo "✔ Plik elevatormusic.mp3 istnieje."
fi

cd ..
echo ""
echo "========================================"
echo "   GOTOWE! (OpenSUSE Edition)          "
echo "========================================"
echo "Jak uruchomić:"
echo "1. Terminal 1: cd server && ./radio_server"
echo "2. Terminal 2: cd client && python3 main.py"
