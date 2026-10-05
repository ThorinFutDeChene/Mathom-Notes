# Feuille de route de Mathom Notes

## Version stable actuelle

~~~text
Mathom Notes 3.4.1
~~~

## Socle réalisé

Le projet dispose désormais notamment de :

- identité Mathom Notes ;
- terminologie Mathom-House / Étagère / Mathom ;
- profil utilisateur propre ;
- compatibilité avec les données BasKet ;
- Pages ;
- tableur ;
- système de diagnostic ;
- mécanisme de mise à jour Debian ;
- Annuler / Refaire global ;
- sélection multiple ;
- profils d'accessibilité ;
- outils DYS / TDAH ;
- outils phonème / graphème ;
- exposant et indice ;
- packaging Debian natif ;
- interface réorganisée pour les petits écrans.

## Priorité actuelle

Après la stabilisation de 3.4.1, la priorité est la maintenance du socle :

- supprimer les branches Git obsolètes ;
- reprendre la documentation ;
- retirer les fichiers de packaging devenus inutiles ;
- éliminer le code mort clairement identifié ;
- conserver les éléments BasKet nécessaires à la compatibilité ;
- améliorer progressivement les tests.

## Nettoyage du dépôt

Les éléments historiques doivent être examinés individuellement.

Candidats au nettoyage :

- ancien constructeur Debian Flatpak/AppImage ;
- ancien Snap BasKet ;
- anciennes configurations CI inutilisées ;
- anciens fichiers de projet KDevelop ;
- documentation BasKet obsolète ;
- blocs de code désactivés définitivement.

Les noms internes nécessaires à la compatibilité ne doivent pas être renommés dans cette opération.

## Accessibilité

Mathom Notes doit continuer à évoluer en donnant une place importante à :

- lisibilité ;
- troubles DYS ;
- TDAH ;
- personnalisation de l'affichage ;
- utilisation sur les ordinateurs scolaires ;
- petits écrans ;
- réduction de la charge visuelle.

## Principe général

Chaque évolution doit respecter trois règles :

1. ne pas perdre les données utilisateur ;
2. conserver la compatibilité nécessaire avec BasKet ;
3. privilégier une interface compréhensible et accessible.
