# Code dormant et fonctionnalités à conserver

Ce document recense les fonctions présentes dans le code de Mathom Notes
qui peuvent être désactivées, partiellement implémentées ou non exposées
dans l'interface, mais qui ne doivent pas être supprimées lors d'un nettoyage.

## 1. Accessibilité et profils DYS / TDAH

Le moteur d'accessibilité est conçu autour de modules pouvant être combinés.

Plusieurs fonctions sont déjà opérationnelles, tandis que d'autres sont
volontairement préparées dans le code pour une implémentation ultérieure.

Fonctions et modules à conserver notamment :

- police adaptée ;
- texte agrandi ;
- espacement des lettres ;
- espacement des mots ;
- interligne ;
- espacement des paragraphes ;
- coloration syllabique ;
- coloration phonémique ;
- mise en évidence des graphèmes ;
- aide aux lettres confondables ;
- alternance visuelle des lignes ;
- guide de lecture ;
- mise en évidence de la ligne active ;
- atténuation des autres lignes ;
- lecture vocale ;
- suivi visuel de la lecture vocale ;
- réduction des distractions ;
- agrandissement des contrôles.

Les blocs commentés dans `MainWindow::showCustomAccessibilityDialog()`
sont volontairement conservés afin d'activer progressivement ces modules.

Les profils présents dans l'interface ne sont pas tous encore raccordés
à un preset dans `AccessibilitySettings::effectiveConfiguration()`.

Ils ne doivent pas être supprimés pour cette raison.

## 2. Carte mentale

Le code historique contient un troisième mode de disposition :

- colonnes ;
- disposition libre ;
- carte mentale.

Les éléments suivants doivent être conservés :

- `MINDMAPS_LAYOUT` ;
- `m_mindMap` ;
- `isMindMap()` ;
- l'attribut XML `mindMap` ;
- les traitements de compatibilité associés.

Le système de Pages actuel ne porte pas encore complètement ce mode.

`PageInfo` gère actuellement essentiellement la disposition libre et
le nombre de colonnes, et `isMindMap()` retourne actuellement faux lorsqu'une
Page est active.

Ce moteur constitue néanmoins la base d'une future intégration du mode
Carte mentale au niveau des Pages et ne doit pas être supprimé.

## 3. Chiffrement

Mathom contient une infrastructure de chiffrement désactivée par défaut
à la compilation avec :

~~~text
ENABLE_GPG=OFF
~~~

Le moteur comprend notamment :

- GPGME / OpenPGP ;
- chiffrement par mot de passe ;
- chiffrement par clé ;
- sélection de clé ;
- chiffrement et déchiffrement ;
- verrouillage et déverrouillage ;
- verrouillage automatique après inactivité ;
- ré-encryption des fichiers d'une Mathom-House.

Les éléments utilisant notamment :

- `ENABLE_GPG` ;
- `HAVE_LIBGPGME` ;
- `KGpgMe` ;
- `PasswordEncryption` ;
- `PrivateKeyEncryption` ;
- `isEncrypted()` ;
- `isFileEncrypted()` ;

doivent être conservés.

Le fait qu'une fonction soit conditionnée par `HAVE_LIBGPGME` ou désactivée
dans la compilation par défaut ne signifie pas qu'elle soit obsolète.

## 4. Compatibilité texte historique

Mathom conserve encore la prise en charge des anciens Mathoms texte brut.

Le moteur de conversion :

~~~text
convertTexts()
slotConvertTexts()
~~~

doit être conservé afin de préserver la compatibilité avec les anciennes
données.

Les anciennes actions d'interface permettant de créer directement un
Mathom texte brut sont actuellement désactivées. Elles doivent être examinées
séparément avant toute suppression.

## Règle de nettoyage

Un bloc commenté, un `#if 0`, un `TODO` ou un `FIXME` ne constitue pas à lui
seul une preuve de code mort.

Avant toute suppression, vérifier si le code relève :

1. d'une fonctionnalité future prévue ;
2. de la compatibilité avec BasKet ou d'anciens fichiers Mathom ;
3. de l'accessibilité ;
4. de la carte mentale ;
5. du chiffrement ;
6. d'un moteur encore utilisé par une fonction active.

Seul le code dont l'abandon est établi et qui n'a aucun de ces rôles peut
être supprimé lors d'un nettoyage.

## 5. Outil développeur basketweaver

Le dossier `devtools/weaver` contient l'utilitaire `basketweaver`.

Il permet notamment :

- d'encoder un dossier compatible en archive `.baskets` ;
- de décoder une archive `.baskets` ;
- de vérifier la structure générale d'une archive ;
- de manipuler l'aperçu PNG associé ;
- de tester et diagnostiquer le format historique.

Cet outil est utile pour la compatibilité, la migration et le diagnostic.

Il doit être conservé.

En revanche, il ne doit pas être compilé ou installé par défaut dans
une construction utilisateur normale.

La valeur par défaut de :

~~~text
BUILD_DEVTOOLS
~~~

doit donc rester à `OFF`.

Un développeur peut explicitement l'activer avec :

~~~bash
cmake -S . -B build -DBUILD_DEVTOOLS=ON
~~~

## 6. Miniatures des archives `.baskets`

Le dossier `file-integration` contient un ancien générateur de miniatures
pour les archives et modèles BasKet.

Le moteur sait lire une archive `.baskets` ou `.baskett`, rechercher le
champ :

~~~text
preview*:<taille>
~~~

et extraire l'image PNG embarquée afin de l'utiliser comme miniature.

Cette fonction est intéressante pour Mathom et doit être conservée.

Son intégration KDE actuelle est cependant incomplète :

- le fichier `basketthumbcreator.desktop` est encore présent ;
- le module `basketthumbcreator` est compilé ;
- mais le fichier Desktop n'est plus installé par le CMake actuel ;
- le code n'utilise pas encore le mécanisme moderne de déclaration
  des plugins KDE6.

Le moteur doit donc être conservé pour une future remise en service,
mais il ne doit pas être confondu avec une intégration KDE6 actuellement
fonctionnelle.

Le fichier `file-integration/basket.xml` est en revanche actif et
indispensable à la compatibilité des types MIME historiques.

### Remise en service KF6 du générateur de miniatures

Lors du nettoyage de 3.4.2, le générateur de miniatures a été audité.

Le moteur était encore compilé et empaqueté, mais installé comme un plugin Qt
générique sans métadonnées KF6 exploitables par KIO.

Il a donc été conservé et modernisé :

- plugin déclaré avec `K_PLUGIN_CLASS_WITH_JSON` ;
- métadonnées JSON intégrées ;
- installation dans `kf6/thumbcreator` ;
- suppression de l'ancien fichier Desktop de service.

Cette fonction doit rester disponible afin d'afficher directement dans le
gestionnaire de fichiers l'aperçu PNG embarqué dans les archives `.baskets`
et modèles `.baskett`.

## 7. Export HTML : conservation des largeurs de colonnes

`HTMLExporter::exportNote()` contient un calcul historique désactivé destiné
à conserver dans l'export HTML les proportions des colonnes redimensionnées.

Ce code ne doit pas être réactivé tel quel.

Le calcul historique mélange actuellement :

- des largeurs exprimées en pixels ;
- une sortie HTML exprimée en pourcentage.

Il peut donc produire des proportions incorrectes, fréquemment ramenées à
100 %.

L'objectif reste utile et le bloc est conservé comme base pour une future
correction avec tests d'export HTML.
