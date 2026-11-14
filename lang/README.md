# ClickDarkMode - Language Translations

This directory contains JSON language translation files for ClickDarkMode.

## Adding a New Language

1. Copy `en-US.json` and rename it to your target language locale code, for example:
   - `de-DE.json` (German)
   - `es-ES.json` (Spanish)
   - `fr-FR.json` (French)
   - `hu-HU.json` (Hungarian)
   - `it-IT.json` (Italian)
   - `ja-JP.json` (Japanese)
   - `pl-PL.json` (Polish)
   - `zh-CN.json` (Simplified Chinese)

2. Update the language header attributes:
   ```json
   {
     "__language_code": "de-DE",
     "__language_name": "Deutsch",
     ...
   }
   ```

3. Translate the strings in the file. Any missing keys will automatically fall back to the default English strings.

4. Place the `.json` file in the `lang\` folder next to `ClickDarkMode.exe`. The application will automatically detect and list the new language in the Settings menu without requiring any code changes or recompilation!
