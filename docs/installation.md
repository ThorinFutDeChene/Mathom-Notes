# Installation et construction de Mathom Notes

## Versions

- **Derniere stable publiee** : 5.0.6.
- **Version de developpement en preparation** : 5.0.7-dev2.
- Depot et code source publics, logiciel libre et gratuit (GPL-2.0-or-later).

## Norme d'installation

Le paquet Debian natif installe l'application dans :

- /opt/mathom/bin/mathom : executable principal ;
- /opt/mathom/lib et /opt/mathom/share : bibliotheques internes, modules de configuration, traductions et ressources ;
- /usr/bin/mathom : lanceur systeme ;
- /usr/share/applications : entree de menu ;
- /usr/share/icons et /usr/share/pixmaps : icones ;
- /usr/share/metainfo et /usr/share/mime : integration au systeme.

Les bibliotheques Qt 6 et KDE Frameworks 6 du systeme restent des dependances APT :
elles ne sont pas copiees dans /opt.

## Donnees utilisateur : aucun deplacement

Mathom conserve les notes et fichiers de travail dans le repertoire XDG
habituel (par defaut ~/.local/share/mathom/), la configuration dans
~/.config/mathomrc et les sauvegardes automatiques dans l'emplacement
interne actuellement calcule par l'application (generalement
~/.local/share/mathom_backup/).

Seuls les exports et les sauvegardes manuelles proposes par defaut
utilisent ~/Documents/Mathom Notes/ (ou le repertoire Documents XDG localise).

Un dossier de destination deja choisi par l'utilisateur est respecte.
La migration du programme ne deplace, n'ecrase et ne supprime aucun profil,
aucune note et aucune sauvegarde automatique.

## Mise a jour d'une installation existante

Installer le nouveau .deb sous le meme nom de paquet mathom :

~~~bash
sudo apt install ./mathom_5.0.7-0dev2_amd64.deb
~~~

APT/dpkg substituent les anciens composants applicatifs places sous /usr
aux composants du paquet sous /opt et a son nouveau lanceur.
Le script d'installation ne manipule jamais les fichiers personnels.

**Important :** la version 5.0.7-dev2 est en developpement ;
ne pas la considerer comme disponible avant publication de la Release DEV
et ses tests d'installation.

## Mises a jour automatiques

En version stable, une verification asynchrone des Releases stables GitHub
est lancee apres l'ouverture. Aucun message si le reseau est indisponible
ou si aucune nouvelle version stable n'est disponible. L'utilisateur reste
libre d'accepter ou de refuser une version stable ; le refus est retenu
pour cette version.

En DEV, pas de verification automatique. Le menu conserve la verification
manuelle, avec le canal developpement lorsque les reglages l'autorisent.

Le telechargement valide le SHA-256 annonce par GitHub et l'identite du
paquet Debian. L'installation demande l'autorisation Polkit et la
relance ne s'effectue qu'avec l'accord de l'utilisateur.

## Construction et controle

~~~bash
./scripts/build-mathom-native-deb.sh

DEB="packaging/mathom_5.0.7-0dev2_amd64.deb"
dpkg-deb -f "$DEB" Package Version Architecture Depends
dpkg-deb -c "$DEB"
sha256sum "$DEB"
~~~

Verifier que l'executable et les composants sont bien sous /opt/mathom,
que /usr/bin/mathom est un lanceur, et que les ressources hicolor,
le fichier desktop et l'integration MIME sont presentes.

Un build reussi ne remplace pas un essai sur le paquet DEV publie.
Tester une premiere installation et une mise a jour depuis 5.0.6.
