# Développement de Mathom Notes

## Base technique

Mathom Notes utilise principalement :

- C++ ;
- Qt 6 ;
- KDE Frameworks 6 ;
- CMake ;
- ECM ;
- LibGit2.

## Identité publique

| Élément | Valeur |
|---|---|
| Application | Mathom Notes |
| Version stable | 3.4.1 |
| Exécutable | `mathom` |
| Desktop ID | `fr.thorinux.mathom` |
| Desktop file | `fr.thorinux.mathom.desktop` |
| Metainfo | `fr.thorinux.mathom.metainfo.xml` |
| Configuration | `mathomrc` |
| Données | espace XDG `mathom/` |

## Héritage interne

Ne pas renommer massivement les structures héritées de BasKet sans nécessité.

Exemples :

- `BasketScene` ;
- `LibBasket` ;
- `basket_SRCS` ;
- `BASKET_VERSION` ;
- domaine de traduction `basket` ;
- formats `.baskets`.

Un changement interne n'est utile que s'il apporte un bénéfice réel supérieur au risque de régression.

## Internationalisation

Toute chaîne visible par l'utilisateur doit utiliser l'infrastructure KDE i18n.

Il ne faut pas coder une nouvelle fonction uniquement en français.

La terminologie Mathom doit être utilisée pour les nouvelles chaînes visibles.

## Versionnement

Version stable :

~~~text
MAJEUR.MINEUR.CORRECTIF
~~~

Version de développement :

~~~text
MAJEUR.MINEUR.CORRECTIF-devN
~~~

Exemples :

~~~text
3.4.1
3.4.2-dev1
3.5.0-dev1
~~~

Une nouvelle fonction visible utilise normalement une nouvelle version mineure.

Un correctif, nettoyage ou refactoring interne utilise normalement une nouvelle version corrective.

## Packaging Debian

Le script de référence actuel est :

~~~text
scripts/build-mathom-native-deb.sh
~~~

Le constructeur natif utilise les bibliothèques Qt 6 et KDE Frameworks 6 du système.

L'ancien constructeur :

~~~text
scripts/build-mathom-deb.sh
~~~

est historique et n'est plus utilisé pour les releases Debian actuelles.

## Workflow

Pour une évolution :

1. partir de `main` ;
2. créer une branche dédiée ;
3. effectuer des changements cohérents ;
4. exécuter `git diff --check` ;
5. compiler ;
6. construire le paquet si nécessaire ;
7. publier `dev1` ;
8. tester ;
9. corriger en `dev2`, `dev3`, etc. ;
10. promouvoir en stable après validation.

Une préversion déjà publiée n'est jamais réécrite.

## Sécurité Git

Ne jamais écraser une branche divergente sans sauvegarde.

Avant un nettoyage :

~~~bash
git fetch origin --prune
git branch -r
~~~

Une branche peut être supprimée lorsqu'elle est entièrement intégrée à `main` et ne contient plus de travail unique à conserver.

## Builds

Pour les builds longs :

~~~bash
./scripts/build-mathom-native-deb.sh \
  > packaging/build-VERSION.log 2>&1
~~~

Éviter `set -e` directement dans le shell interactif.

S'il est nécessaire, l'utiliser dans un sous-shell :

~~~bash
(
  set -e
  commande1
  commande2
)
~~~

## Contrôles avant publication

Vérifier notamment :

~~~bash
git status --short
git diff --check
~~~

Pour un paquet Debian :

~~~bash
dpkg-deb -f paquet.deb Package Version Architecture Depends
dpkg-deb -c paquet.deb
sha256sum paquet.deb
~~~

Le paquet natif Mathom ne doit pas contenir de runtime :

~~~text
/opt/mathom
~~~
