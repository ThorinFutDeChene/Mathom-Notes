# Format des archives `.baskets`

Mathom Notes conserve le format d'archive historique de BasKet Note Pads
afin de préserver la compatibilité avec les données existantes.

Les informations ci-dessous ont été vérifiées avec l'implémentation actuelle
de `src/archive.cpp`.

## Identifiant du format

Une archive commence par :

~~~text
BasKetNP:archive
~~~

Cet identifiant historique fait partie du format de fichier et ne doit pas
être renommé en `Mathom` sans création d'un nouveau format et d'un mécanisme
de migration.

## Version actuelle du format

Mathom écrit actuellement :

~~~text
version:0.6.1
~~~

Le moteur sait également analyser les champs historiques :

~~~text
read-compatible
write-compatible
~~~

lorsqu'ils sont présents dans une archive.

## Structure générale

Une archive `.baskets` contient successivement :

1. l'en-tête du format ;
2. la version ;
3. l'en-tête de l'aperçu ;
4. éventuellement une image d'aperçu PNG ;
5. l'en-tête de l'archive interne ;
6. une archive `tar.gz` contenant les données.

Schéma historique conservé :

![Structure d'une archive baskets](images/archives-baskets/basket-archive-file-structure.png)

## En-tête

Exemple :

~~~text
BasKetNP:archive
version:0.6.1
~~~

## Aperçu

L'aperçu est annoncé par :

~~~text
preview*:<taille>
~~~

où `<taille>` indique la taille en octets du flux qui suit.

Exemple :

~~~text
preview*:12000
~~~

L'aperçu est une image PNG.

## Archive interne

Après l'aperçu, l'archive interne est annoncée par :

~~~text
archive*:<taille>
~~~

Exemple :

~~~text
archive*:1245000
~~~

Le contenu correspondant est une archive gzip/tar.

L'implémentation actuelle utilise `KTar` avec :

~~~text
application/x-gzip
~~~

## Contenu interne

L'archive contient notamment une arborescence `baskets/`.

Le moteur d'export crée notamment :

~~~text
baskets/baskets.xml
tags.xml
~~~

ainsi que les répertoires et fichiers nécessaires aux Mathom-Houses,
aux Mathoms, aux icônes, aux arrière-plans et aux autres ressources
contenues dans les données exportées.

Le nom `baskets` est historique et fait partie de la compatibilité du format.

## Exemple de flux

L'ancien projet BasKet fournissait également un exemple visuel de la
structure binaire d'une archive :

![Exemple de contenu d'une archive baskets](images/archives-baskets/basket-archive-file-content-example.png)

Les fichiers SVG originaux de ces deux schémas sont également conservés
dans `docs/images/archives-baskets/`.

## Compatibilité

Les éléments suivants doivent être considérés comme des éléments de format
et non comme de simples références visuelles à BasKet :

- extension `.baskets` ;
- en-tête `BasKetNP:archive` ;
- version de format `0.6.1` ;
- répertoire interne `baskets/` ;
- métadonnées de compatibilité de version.

Ils ne doivent pas être renommés lors d'un nettoyage cosmétique du code.

## Outil de diagnostic

L'utilitaire développeur `basketweaver`, présent dans `devtools/weaver`,
permet notamment :

- d'encoder un dossier en `.baskets` ;
- de décoder une archive `.baskets` ;
- de contrôler superficiellement la structure du fichier.

Il constitue un outil utile pour le diagnostic et la compatibilité du format.
