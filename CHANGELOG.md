# Changelog Mathom Notes

Ce fichier résume les principales versions propres au fork Mathom.

Le journal de développement détaillé reste la référence technique complète du projet.

## 3.7.0

### Added

- nouveau format natif d’échange `.mathom` avec signature `MathomNotes:archive` ;
- export de la Mathom-House courante avec ses Étagères, Pages, Mathoms, pièces jointes, marques et arrière-plans nécessaires ;
- export de toutes les Mathom-Houses dans une seule archive `.mathom` ;
- import des archives Mathom `.mathom` ;
- gestion des conflits de noms à l’import avec proposition de renommage automatique ;
- traces de diagnostic dédiées aux opérations d’export Mathom.

### Changed

- conservation de la compatibilité d’import avec les anciennes archives BasKet `.baskets` ;
- sélecteurs de fichiers adaptés à Qt 6 ;
- ajout automatique de l’extension `.mathom` lors de l’export.

### Fixed

- correction des plantages d’export liés à la restauration d’une sélection vide ;
- sécurisation de la restauration des sélections lors de la génération des miniatures d’archives.

## 3.4.1

### Changed

- réorganisation de la barre d'outils selon la fréquence réelle d'utilisation ;
- fusion de la barre principale et de la barre de mise en forme ;
- priorité donnée aux commandes de prise de notes et de mise en forme sur les petits écrans ;
- alignements placés avant les commandes Couper / Copier / Coller / Supprimer.

### Removed

- suppression complète de la fonction de navigation historique Précédent / Suivant ;
- suppression de sa pile d'historique, de ses raccourcis et de ses actions associées.

## 3.4.0

### Added

- exposant et indice dans l'éditeur de texte riche ;
- prise en charge des sélections multiples ;
- persistance de l'alignement vertical ;
- outils x² et x₂ dans le tableur ;
- conversion Unicode des exposants et indices dans les cellules ;
- protection des formules du tableur.

## 3.3.1

### Added

- historique global Annuler / Refaire pour les modifications de données.

## 3.3.0

### Changed

- amélioration de l'édition et de la gestion du focus ;
- amélioration de la sélection multiple.

## 3.2.0

### Added

- packaging Debian natif ;
- informations À propos de Mathom ;
- intégration du financement participatif Ulule.

## 3.1.0

### Added

- outils phonème / graphème.

## 3.0.0

### Added

- menu Profil ;
- profils d'accessibilité cumulables ;
- profils DYS et TDAH ;
- profil personnalisé ;
- police OpenDyslexic ;
- agrandissement et réglages d'espacement ;
- coloration syllabique.

## Versions antérieures

Les séries antérieures correspondent à la transformation progressive de BasKet Note Pads vers Mathom Notes :

- identité Mathom ;
- terminologie Mathom-House / Étagère / Mathom ;
- migration et compatibilité BasKet ;
- nouvelles icônes ;
- Pages ;
- tableur ;
- diagnostics ;
- mises à jour Debian ;
- corrections et stabilisation du fork.
