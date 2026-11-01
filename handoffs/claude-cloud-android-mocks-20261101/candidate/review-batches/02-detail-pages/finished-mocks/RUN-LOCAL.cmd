@echo off
cd /d "%~dp0"
echo Open http://127.0.0.1:8766/review-batches/02-detail-pages/finished-mocks/index.html
echo Stop this local preview with Ctrl+C.
python -m http.server 8766 --bind 127.0.0.1 --directory "%~dp0..\..\.."
