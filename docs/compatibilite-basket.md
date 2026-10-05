# Compatibilité avec BasKet Note Pads

## Principe

Mathom Notes est issu de BasKet Note Pads.

La compatibilité avec les données historiques fait partie des contraintes du projet.

## Archives `.baskets`

Les archives historiques utilisent notamment :

~~~text
BasKetNP:archive
~~~

Cet identifiant appartient au format de fichier.

Il ne doit pas être renommé uniquement parce que l'application porte désormais le nom Mathom Notes.

## Types MIME

Certains types MIME historiques conservent également le nom Basket, par exemple :

~~~text
application/x-basket-archive
application/x-basket-template
application/x-basket-item
~~~

Ils participent à la compatibilité des fichiers existants.

## Noms internes

Le code contient encore volontairement des éléments tels que :

- `BasketScene` ;
- `BasketFactory` ;
- `BasketListViewItem` ;
- `LibBasket` ;
- variables `BASKET_*` ;
- fichiers `basket*.cpp`.

Ces noms internes ne correspondent pas nécessairement à l'identité affichée à l'utilisateur.

## Version historique CMake

Le projet CMake conserve actuellement :

~~~text
project(Basket VERSION 2.50.90)
~~~

La version publique de Mathom est gérée séparément par le fichier :

~~~text
VERSION
~~~

Cette séparation permet de faire évoluer Mathom sans effectuer un renommage massif risqué de l'architecture héritée.

## Traductions

Le domaine de traduction historique `basket` est encore utilisé.

Son changement nécessiterait une migration coordonnée des catalogues de traduction et ne doit pas être effectué dans le cadre d'un simple nettoyage.

## Règle

Une ancienne référence à BasKet peut être supprimée lorsqu'elle est :

- uniquement cosmétique ;
- obsolète ;
- sans rôle dans la compatibilité.

Elle doit être conservée lorsqu'elle intervient dans :

- un format de fichier ;
- les données utilisateur ;
- les traductions ;
- l'ABI ;
- l'import ;
- les types MIME ;
- la compatibilité avec une ancienne installation.
