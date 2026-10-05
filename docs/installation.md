# Installation et construction

## Version stable

~~~text
Mathom Notes 3.4.1
~~~

## Installation du paquet Debian

Télécharger le paquet Debian depuis les Releases GitHub.

Pour Mathom Notes 3.4.1 :

~~~text
mathom_3.4.1-1_amd64.deb
~~~

Installation :

~~~bash
sudo apt install ./mathom_3.4.1-1_amd64.deb
~~~

Mathom utilise les bibliothèques Qt 6 et KDE Frameworks 6 du système.

Le paquet Debian natif actuel ne déploie pas de runtime autonome sous `/opt/mathom`.

## Construction du paquet

Le constructeur officiel est :

~~~bash
./scripts/build-mathom-native-deb.sh
~~~

Le script :

1. configure le projet avec CMake ;
2. compile Mathom ;
3. installe les fichiers dans une racine Debian temporaire ;
4. construit le paquet avec `dpkg-deb` ;
5. effectue les contrôles de packaging.

Pour la version 3.4.1 :

~~~text
packaging/mathom_3.4.1-1_amd64.deb
~~~

## Journal de compilation

Pour conserver la sortie d'un build :

~~~bash
./scripts/build-mathom-native-deb.sh \
  > packaging/build-3.4.1.log 2>&1
~~~

## Contrôle du paquet

~~~bash
DEB="packaging/mathom_3.4.1-1_amd64.deb"

dpkg-deb -f "$DEB" Package Version Architecture Depends
dpkg-deb -c "$DEB"
sha256sum "$DEB"
~~~

## Compilation directe

Versions minimales actuellement déclarées :

- CMake 3.16 ;
- Qt 6.5 ;
- KDE Frameworks 6.9 ;
- LibGit2 1.8.4 lorsque le support Git est activé.

Exemple :

~~~bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DBUILD_DEVTOOLS=OFF

cmake --build build
~~~

## Ancien packaging

Le dépôt possède encore historiquement :

~~~text
scripts/build-mathom-deb.sh
~~~

Ce constructeur basé sur Flatpak et un runtime autonome n'est plus le constructeur officiel des releases Debian actuelles.

Il doit être considéré comme historique en attendant son nettoyage du dépôt.
