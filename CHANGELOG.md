# Changelog Mathom Notes

Ce fichier résume les principales versions propres au fork Mathom.

Le journal de développement détaillé reste la référence technique complète du projet.

## 3.12.0

### Added

- raccourcis clavier configurables pour le renommage et les proprietes des Pages ;
- raccourcis configurables pour les conversions entre Etageres, Pages et Mathom-Houses ;
- douze raccourcis configurables pour le tableur : ajout/suppression de lignes et colonnes, formules et exposants/indices ;
- barre d'organisation **+ / - / Trier** au-dessus des Mathom-Houses et Etageres ;
- deux actions configurables distinctes pour supprimer une Mathom-House ou une Etagere.

### Changed

- quatre tris ponctuels : Etageres A-Z / Z-A au niveau selectionne, Mathom-Houses A-Z / Z-A ;
- conservation du classement obtenu, de l'ajout en bas de liste et du deplacement manuel ;
- suppression de l'entree Trier redondante du menu superieur, avec conservation du clic droit ;
- conservation des identifiants des anciennes actions et des raccourcis personnalises.

### Validation et limites

- prereleases 3.12.0-dev1 a dev5 publiees et validees fonctionnellement ;
- Annuler / Refaire disponible pour le tri pendant la session ;
- aucun raccourci par defaut impose aux nouvelles actions ;
- l'importation de PDF et la reconnaissance OCR/HTR ne font pas partie de cette version.

## 3.11.0

- parametres avances et configuration des raccourcis clavier ;
- historiques Annuler / Refaire ameliores : 100 operations en session ;
- reprise gardee de certaines operations sur 24 heures (20 par document) apres reouverture ;
- identifiants persistants des Mathoms et verification d'integrite pour la recuperation.

## 3.10.2

- internationalisation synchronisee sur 38 catalogues, dont 12 variantes entierement traduites ;
- etiquettes predefinies traduites en conservant les libelles personnalises.

## 3.10.1

- uniformisation des chaines sources internationalisables ;
- correction des pluriels et completude du catalogue francais.

## 3.10.0

- conversions entre Mathom-Houses, Etageres et Pages ;
- conservation des Mathoms et integration a Annuler / Refaire.

## 3.9.1

- boutons + et - pour creer/supprimer les Pages, avec confirmations et historique.

## 3.9.0

- export HTML prenant en charge les Pages, sous-Etageres et liens internes ;
- export PDF de la Page courante avec rendu de sa disposition.

## 3.8.0

- sauvegardes et restaurations manuelles controlees ;
- sauvegardes automatiques quotidiennes, hebdomadaires et mensuelles.

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
