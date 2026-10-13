# Block Launcher

Launcher Minecraft : Java Edition en C++ / Qt 6, au style Windows 11 (thème sombre Fluent, effet Mica, coins arrondis, icônes Segoe Fluent).

## Fonctionnalités

- Connexion avec ton compte Microsoft (OAuth dans le navigateur, session mémorisée et renouvelée automatiquement)
- Liste de toutes les versions officielles (finales + snapshots en option), recherche
- Téléchargement parallèle du jeu, des bibliothèques et des ressources, avec vérification SHA-1
- Java est téléchargé automatiquement (runtime officiel de Mojang, la bonne version pour chaque version du jeu)
- Réglage de la RAM, arguments Java, fermeture du launcher au démarrage du jeu
- Les fichiers vont dans `%APPDATA%\.blocklauncher` : ton `.minecraft` officiel n'est pas touché

## Compiler sous Windows

1. Installe **Qt 6.4 ou plus** avec le [Qt Online Installer](https://www.qt.io/download-qt-installer) (composant *MSVC 2022 64-bit*) et **Visual Studio 2022** (charge de travail « Développement desktop en C++ »).
2. Ouvre `CMakeLists.txt` dans **Qt Creator**, choisis le kit MSVC 64-bit, puis *Compiler* (Ctrl+B) et *Exécuter* (Ctrl+R).

En ligne de commande (depuis « x64 Native Tools Command Prompt for VS 2022 ») :

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64
cmake --build build --config Release
C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe build\Release\BlockLauncher.exe
```

`windeployqt` copie les DLL Qt à côté de l'exe pour que tu puisses le lancer ou le partager.

## Obtenir un ID client Azure (obligatoire pour se connecter)

Microsoft n'autorise la connexion qu'aux applications enregistrées :

1. Va sur [portal.azure.com](https://portal.azure.com) → **Microsoft Entra ID** → **Inscriptions d'applications** → **Nouvelle inscription**.
2. Types de comptes : **Comptes Microsoft personnels uniquement**.
3. URI de redirection : plateforme **Client public/natif (mobile et bureau)**, valeur `http://localhost:5173/callback`.
4. Copie l'**ID d'application (client)** et colle-le dans *Paramètres → ID client Azure* du launcher.
5. Demande à Mojang l'accès à l'API Minecraft pour cet ID via le formulaire [aka.ms/mce-reviewappid](https://aka.ms/mce-reviewappid). Tant que ce n'est pas approuvé, la connexion échoue avec un message qui l'indique.

Tu peux aussi définir la variable d'environnement `BLOCK_CLIENT_ID`.

## Structure

| Fichier | Rôle |
|---|---|
| `src/MainWindow.*` | Interface : barre latérale, pages Accueil / Versions / Paramètres, contrôles Fluent |
| `src/Style.h` | Feuille de style Windows 11 et icônes |
| `src/Auth.*` | Connexion Microsoft → Xbox Live → Minecraft |
| `src/Game.*` | Téléchargement des versions, de Java, et lancement |
| `src/Net.h` | Petits utilitaires HTTP |

## Limites actuelles

- Pas encore de versions moddées (Fabric, Forge…)
- Il faut un compte qui possède Minecraft : Java Edition

Projet non officiel, non affilié à Mojang Studios ni à Microsoft.
