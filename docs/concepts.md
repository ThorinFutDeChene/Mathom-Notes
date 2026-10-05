# Concepts et vocabulaire

## Mathom

Dans Mathom Notes, une information n'est pas nécessairement une simple note.

Un Mathom peut notamment être :

- un texte ;
- une image ;
- un lien ;
- un fichier ;
- un lanceur ;
- un contenu de tableur.

Le terme **Mathom** désigne l'unité d'information manipulée dans l'application.

## Hiérarchie

### Mathom-House

Une **Mathom-House** est l'espace principal de classement.

Elle correspond au niveau historiquement appelé « basket » dans BasKet Note Pads.

### Étagère

Une **Étagère** est une subdivision hiérarchique d'une Mathom-House.

Une Étagère peut elle-même contenir d'autres Étagères.

### Page

Une Mathom-House ou une Étagère peut comporter plusieurs **Pages**.

Les Pages permettent de séparer différents espaces de travail à l'intérieur d'un même emplacement.

### Mathom

Un **Mathom** est l'élément manipulé sur une Page.

Les Mathoms peuvent être déplacés, regroupés et organisés visuellement.

## Organisation

Mathom Notes conserve une organisation souple héritée de BasKet :

- déplacement par glisser-déposer ;
- ordre libre des Mathoms ;
- regroupement de plusieurs Mathoms ;
- déplacement entre Pages ;
- déplacement entre Étagères ;
- déplacement entre Mathom-Houses.

## Vocabulaire interne

Une partie du code conserve volontairement des noms historiques afin d'éviter les régressions.

| Interface Mathom | Nom interne historique possible |
|---|---|
| Mathom-House | Basket |
| Étagère | Basket / sous-basket |
| Mathom | Note |
| Page | Page |

Des classes telles que `BasketScene`, `BasketFactory` ou `LibBasket` ne doivent pas être renommées uniquement pour des raisons esthétiques.

La stabilité et la compatibilité priment sur le renommage interne.
