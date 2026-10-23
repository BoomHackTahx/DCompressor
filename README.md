# DCompressor

DCompressor est une application de bureau multiplateforme pour créer, parcourir et extraire des archives. Elle est développée en C++ avec Qt 6 et libarchive.

## Fonctionnalités

- Parcourir le contenu d’une archive et consulter le type, la taille et la date de modification des entrées.
- Créer une archive à partir de fichiers sélectionnés ou d’un dossier complet.
- Extraire une archive ou une entrée sélectionnée.
- Lire les formats pris en charge par la version de libarchive installée.
- Créer des archives ZIP, 7z, TAR, TAR.GZ, TAR.BZ2, TAR.XZ et TAR.LZMA.

La prise en charge des formats dépend de la compilation de libarchive. Les archives protégées par mot de passe, les volumes fractionnés et la modification d’archives existantes ne sont pas encore pris en charge.

## Essayer sous Windows

Le moyen le plus simple est de télécharger le paquet prêt à l’emploi :

1. Ouvrir l’onglet **Actions** du dépôt GitHub.
2. Sélectionner la dernière exécution réussie du workflow **Build**.
3. Dans la section **Artifacts**, télécharger `DCompressor-windows-x64`.
4. Décompresser le ZIP et lancer `dcompressor.exe`.

Le paquet contient l’application et ses bibliothèques Qt/libarchive. Le runtime Microsoft Visual C++ 2015-2022 (x64) peut aussi être nécessaire : [le télécharger depuis Microsoft](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist).

Pour compiler depuis les sources, il faut Windows 10/11 64 bits, Git et Visual Studio 2022 ou Build Tools avec la charge de travail **Développement Desktop en C++**. Installer ensuite [vcpkg](https://learn.microsoft.com/vcpkg/) et définir `VCPKG_ROOT` sur son dossier d’installation. Depuis PowerShell, à la racine du projet :

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Le manifeste `vcpkg.json` installe Qt 6 et libarchive automatiquement. L’exécutable compilé se trouve dans `build/Release/dcompressor.exe`.

## Essayer sous Linux

Il faut un compilateur C++20, CMake 3.20 ou plus récent, Qt 6 Widgets et libarchive.

Sur Ubuntu ou Debian :

```sh
sudo apt update
sudo apt install build-essential cmake qt6-base-dev libarchive-dev
```

Sur Arch Linux :

```sh
sudo pacman -S --needed base-devel cmake qt6-base libarchive
```

Puis, à la racine du projet :

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/dcompressor
```

## Compiler avec CMake

Les compilations Linux et Windows sont vérifiées automatiquement par GitHub Actions à chaque push et pull request. Le workflow fournit un ZIP Windows téléchargeable dans les artefacts de l’exécution.

## Licence

Aucune licence n’a encore été choisie. En attendant, tous les droits restent réservés par le titulaire du droit d’auteur.# DCompressor

DCompressor is a cross-platform C++ desktop archive utility built with Qt 6 and libarchive.

## Features

- Browse archive contents and view entry type, size, and modification time.
- Create archives from selected files or a complete folder.
- Extract archives with protections against path traversal and unsafe symlinks.
- Read supported formats handled by libarchive.
- Create ZIP, 7z, TAR, TAR.GZ, TAR.BZ2, TAR.XZ, and TAR.LZMA archives.

Format support can vary with the libarchive build installed on your system. Password-protected archives, split volumes, editing existing archives, and per-entry extraction are not implemented yet.

## Requirements

- C++20 compiler
- CMake 3.20 or newer
- Qt 6 Widgets
- libarchive

On Windows, install [vcpkg](https://learn.microsoft.com/vcpkg/) and set `VCPKG_ROOT` to its installation directory. The project manifest installs Qt 6 and libarchive automatically when CMake is configured with the vcpkg toolchain:

```powershell
cmake -S . -B build `
	-DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
	-DVCPKG_TARGET_TRIPLET=x64-windows `
	-DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Arch Linux, install the dependencies with:

```sh
sudo pacman -S --needed base-devel cmake qt6-base libarchive
```

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/dcompressor
```

GitHub Actions builds the application on Linux and Windows. The Windows job bundles the executable and its Qt/libarchive runtime dependencies as a downloadable `DCompressor-windows-x64.zip` artifact.

## License

No license has been selected yet. Until one is added, all rights remain reserved by the copyright holder.