@echo off
setlocal enabledelayedexpansion

echo ==================================================
echo   Starting batch testing...
echo   Results will be saved to results_final.txt
echo ==================================================

:: Initialize the result file
echo Final Predictor Test Report > results_final.txt
echo Date: %date% %time% >> results_final.txt
echo -------------------------------------------------- >> results_final.txt

:: Loop through all .gz files in the traces folder
for %%f in (traces\*.gz) do (
    echo [Running]: %%f
    echo Trace: %%f >> results_final.txt
    
    :: Run the predictor and append the MPKI line to the text file
    .\predictor.exe "%%f" | findstr "MISPRED_PER_1K_INST" >> results_final.txt
    
    echo [Done]
)

echo.
echo ==================================================
echo   All tests finished! Please open results_final.txt
echo ==================================================
pause