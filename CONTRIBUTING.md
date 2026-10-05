# Contribuer à Mathom Notes

Mathom Notes est un fork de BasKet Note Pads.

## Principes

Toute contribution doit privilégier :

- la stabilité ;
- la compatibilité des données ;
- l'accessibilité ;
- la clarté de l'interface ;
- le multilingue ;
- la conservation des données utilisateur ;
- des évolutions progressives plutôt qu'un renommage massif risqué.

## Avant une modification

Vérifier que la modification :

- compile avec Qt 6 et KDE Frameworks 6 ;
- n'introduit pas de texte utilisateur non traduit ;
- ne dégrade pas les profils Mathom existants ;
- ne modifie pas un profil BasKet source lors d'une migration ;
- ne casse pas les archives `.baskets` ;
- ne renomme pas une structure historique nécessaire à la compatibilité sans migration explicite.

## Branches

Utiliser une branche par évolution.

Exemples :

~~~text
feature/...
fix/...
cleanup/...
docs/...
packaging/...
ux/...
~~~

Une branche entièrement intégrée dans `main` doit être supprimée après publication.

## Versionnement

Version stable :

~~~text
MAJEUR.MINEUR.CORRECTIF
~~~

Préversion :

~~~text
X.Y.Z-devN
~~~

Paquet Debian de développement :

~~~text
X.Y.Z-0devN
~~~

Paquet Debian stable :

~~~text
X.Y.Z-1
~~~

Une nouvelle capacité visible entraîne normalement une nouvelle version mineure.

Un correctif, un nettoyage ou une amélioration interne utilise normalement le niveau correctif.

## Workflow

1. partir de `main` ;
2. créer une branche dédiée ;
3. effectuer une modification cohérente ;
4. exécuter `git diff --check` ;
5. compiler ;
6. construire le paquet Debian si nécessaire ;
7. publier une préversion ;
8. tester ;
9. corriger en `devN+1` si nécessaire ;
10. promouvoir en stable après validation.

Une préversion publiée n'est jamais réécrite.

## Packaging

Le constructeur Debian officiel est :

~~~text
scripts/build-mathom-native-deb.sh
~~~

L'ancien constructeur autonome Flatpak/AppImage n'est plus utilisé pour les releases Debian actuelles.
