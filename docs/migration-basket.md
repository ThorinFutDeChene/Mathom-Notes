# Migration depuis BasKet

Mathom Notes conserve un mécanisme de migration depuis BasKet Note Pads.

## Objectif

La migration permet de récupérer les données d'une installation BasKet existante sans modifier le profil BasKet d'origine.

Les données sont copiées vers le profil propre à Mathom.

## Profil Mathom

Mathom utilise notamment :

~~~text
~/.local/share/mathom
~~~

pour ses données dans une configuration XDG Linux classique.

La configuration utilise :

~~~text
~/.config/mathomrc
~~~

Les chemins réels peuvent dépendre des variables XDG de l'environnement.

## Sources BasKet

Mathom peut rechercher des profils historiques BasKet.

### Installation native

Exemples habituels :

~~~text
~/.local/share/basket
~/.config/basketrc
~~~

### Flatpak BasKet

Exemples habituels :

~~~text
~/.var/app/org.kde.basket/data/basket
~/.var/app/org.kde.basket/config/basketrc
~~~

## Principe de sécurité

La migration ne doit pas modifier ni supprimer les données BasKet d'origine.

Le principe est :

1. détecter un profil compatible ;
2. copier les données ;
3. créer le profil Mathom ;
4. laisser intact le profil BasKet source.

## Compatibilité

Après migration, Mathom conserve les mécanismes nécessaires à la lecture des données héritées.

Cela inclut notamment certains noms internes et formats historiques.

## Recommandation

Avant toute opération importante sur un ancien profil BasKet, conserver une sauvegarde indépendante des données utilisateur.
