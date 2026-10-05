# Mathom Notes

![Logo Mathom](logo.png)

**Mathom Notes** est un logiciel libre de prise de notes et d'organisation d'informations pour Linux, développé à partir de **BasKet Note Pads**.

La version stable actuelle est **Mathom Notes 3.4.1**.

## Concepts

Mathom utilise trois niveaux principaux :

- **Mathom-House** : espace principal de classement ;
- **Étagère** : subdivision hiérarchique d'une Mathom-House ;
- **Mathom** : note ou contenu conservé dans l'application.

Un Mathom peut notamment contenir du texte, des liens, des images, des fichiers, des lanceurs ou d'autres contenus.

## Fonctionnalités principales

Mathom Notes conserve les fonctions historiques utiles de BasKet et ajoute progressivement ses propres évolutions :

- organisation hiérarchique en Mathom-Houses et Étagères ;
- Mathoms texte riche, images, fichiers, liens et lanceurs ;
- déplacement et regroupement des Mathoms ;
- Pages dans les Mathom-Houses et Étagères ;
- tableur intégré ;
- Annuler / Refaire global ;
- sélection multiple dans l'éditeur ;
- exposant et indice dans le texte riche ;
- exposant et indice Unicode dans le tableur ;
- profils d'accessibilité ;
- prise en charge de profils DYS et TDAH ;
- profil personnalisé ;
- police OpenDyslexic ;
- réglages d'espacement et d'agrandissement ;
- coloration syllabique ;
- outils phonème / graphème ;
- interface optimisée pour les petits écrans ;
- système de diagnostic ;
- vérification et installation des mises à jour Debian.

## Installation

La méthode recommandée est l'installation du paquet Debian natif disponible dans les Releases GitHub.

Exemple pour Mathom Notes 3.4.1 :

~~~bash
sudo apt install ./mathom_3.4.1-1_amd64.deb
~~~

Le paquet utilise les bibliothèques Qt 6 et KDE Frameworks 6 du système.

Il ne déploie pas de runtime autonome sous `/opt/mathom`.

## Construction du paquet Debian

Le script officiel actuel est :

~~~bash
./scripts/build-mathom-native-deb.sh
~~~

Le paquet stable 3.4.1 produit est :

~~~text
packaging/mathom_3.4.1-1_amd64.deb
~~~

L'ancien script autonome basé sur Flatpak/AppImage n'est plus le chemin de construction de référence.

## Identité technique

| Élément | Valeur |
|---|---|
| Nom | Mathom Notes |
| Version stable | 3.4.1 |
| Exécutable | `mathom` |
| Desktop ID | `fr.thorinux.mathom` |
| Fichier desktop | `fr.thorinux.mathom.desktop` |
| Metainfo | `fr.thorinux.mathom.metainfo.xml` |
| Configuration | `mathomrc` |
| Données utilisateur | espace XDG `mathom/` |
| Licence principale | GPL-2.0-or-later |

## Compatibilité avec BasKet

Mathom Notes est un fork de BasKet Note Pads et conserve volontairement plusieurs éléments techniques historiques afin de préserver la compatibilité.

Cela concerne notamment :

- les archives `.baskets` ;
- l'en-tête historique `BasKetNP:archive` ;
- certains noms internes tels que `BasketScene`, `LibBasket` ou `BASKET_VERSION` ;
- le domaine de traduction historique ;
- certains chemins de compatibilité pour les données et ressources.

Ces éléments ne doivent pas être renommés sans mécanisme de migration explicite.

## Documentation

La documentation du projet est disponible dans le dossier [`docs/`](docs/README.md).

## Licence et crédits

Mathom Notes est distribué sous licence **GPL-2.0-or-later**.

Le projet conserve les copyrights, licences et crédits du projet BasKet Note Pads dont il est issu.
