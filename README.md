# sh-chat, le compétiteur de Discord (ou presque)

## Présentation

sh-chat est un super logiciel de chat qui s'utilise depuis votre terminal,
et qui permet de discuter avec vos amis (ou du moins les gens qui prétendent
être vos amis) en toute simplicité.

Le logiciel permet de facilement vous connecter à un serveur de chat, de
rejoindre des salons de discussion, et de discuter avec les autres utilisateurs
du serveur. Vous pouvez même partager des fichiers avec vos amis, et ce sans
limite de taille ! _(Peut devenir à l'avenir une fonctionalité premium super
chère nommée **sh-chat Nitro Pro Max Plus Unlimited**)_

Ce logiciel est performant, et ne comporte aucune fuite de mémoire (normalement).

## Utilisation

### Prérequis

Pour utiliser ce logiciel, vous aurez besoin de :

- `gcc` (ou tout autre compilateur C, merci de modifier les scripts `run.sh` en conséquence).
- Un ordinateur qui s'allume.
- Un OS Unix-like (Linux, MacOS, BSD, etc.).

### Démarrer le serveur

Pour démarrer le serveur, rendez vous dans le dossier `server` et exécutez la
commande suivante :

```sh
./run.sh <Adresse IP> <Port>
```

### Démarrer le client

Pour démarrer le client, rendez vous dans le dossier `client` et exécutez la
commande suivante :

```sh
./run.sh <Adresse IP> <Port>
```

## Fonctionnalités

Une fois le logiciel lancé, vous devriez être positionné sur le salon `#general`.
Vous pouvez alors tapez des messages ou des commandes à envoyer.

### Liste des commandes disponibles :

- `/switch #<nom-du-salon>` : Permet de changer de salon de discussion.
- `/create #<nom-du-salon>` : Permet de créer un nouveau salon de discussion.
- `/send-file <chemin-du-fichier>` : Permet d'envoyer un fichier à tous les
  utilisateurs du salon.
- `/download <uuid-du-fichier>`: Permet de télécharger le fichier envoyé par un
  utilisateur.
- `/nick <nom-utilisateur>`: Permet de changer de pseudo.
- `/exit`: Permet de quitter le programme.

> **⚠️ Attention :** Ce logiciel est encore en phase de développement. Certaines fonctionnalités peuvent ne pas fonctionner comme prévu, et des bugs peuvent être présents. Utilisez-le à vos propres risques.
> Risque d'explosion non quantifié.
