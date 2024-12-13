#include <arpa/inet.h> // Pour inet_pton()
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include "../state.c"
#include "../my_file_struct.c"

#define MAX_CLIENTS 100

int server_socket; // Rendre server_socket global

int client_sockets[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

State server_state;

MyFileStruct* stored_files;
int stored_files_count = 0;

/**
 * @brief Envoie l'état actuel du serveur à tous les clients
 *
 * @param state {State*} L'état à envoyer.
 * @return {void}
 */
void broadcast_state(State* state) {
  size_t state_size;
  char* serialized_state = serialize_state(state, &state_size);
  if (!serialized_state) {
    perror("- x - Échec de la sérialisation de l'état du serveur\n");
    return;
  }

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (client_sockets[i] != -1) {
      if (write(client_sockets[i], &state_size, sizeof(size_t)) == -1) {
        perror("- x - Erreur lors de l'envoi de la taille de l'état du serveur\n");
        continue;
      }

      if (write(client_sockets[i], serialized_state, state_size) == -1) {
        perror("- x - Erreur lors de l'envoi de l'état du serveur\n");
        continue;
      }
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  free(serialized_state);
}

/**
 * @brief Modifier le comportement lors d'un kill
 *        dans le terminal (^c) afin de s'assurer
 *        que le socket est bien refermé.
 *
 * @param sig {int} Non utilisé, nécessaire pour signal()
 * @return {void} Quitte le programme.
 */
void handle_sigint(int sig) {
  printf("\nSignal %d capturé, fermeture du socket et sortie...\n", sig);
  close(server_socket);
  exit(0);
}

/**
 * @brief Gère un client dans un thread distinct.
 *
 * @param client_socket {int*} Le socket du client
 * @return {void*}
 */
void* handle_client(void* client_socket) {
  int sock = *(int*)client_socket;
  // Trouver un emplacement vide dans le tableau client_sockets
  int socket_index = -1;
  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (client_sockets[i] == -1) {
      socket_index = i;
      break;
    }
  }
  pthread_mutex_unlock(&clients_mutex);

  if (socket_index == -1) {
    perror("- x - Trop de clients connectés\n");
    close(sock);
    free(client_socket);
    pthread_exit(NULL);
  }

  client_sockets[socket_index] = sock;
  free(client_socket);

  // Envoyer l'état actuel du serveur au client
  size_t state_size;
  char* serialized_state = serialize_state(&server_state, &state_size);
  if (!serialized_state) {
    perror("- x - Échec de la sérialisation de l'état du serveur\n");
    close(sock);
    pthread_exit(NULL);
  }

  printf("- i - Envoi de l'état actuel - Envoi de la taille\n");
  if (write(sock, &state_size, sizeof(size_t)) == -1) {
    perror("- x - Erreur lors de l'envoi de la taille de l'état du serveur\n");
    free(serialized_state);
    close(sock);
    pthread_exit(NULL);
  }

  printf("- i - Envoi de l'état actuel - Envoi du contenu\n");

  if (write(sock, serialized_state, state_size) == -1) {
    perror("- x - Erreur lors de l'envoi de l'état du serveur\n");
    free(serialized_state);
    close(sock);
    pthread_exit(NULL);
  }

  printf("- i - Envoi de l'état avec succès.\n");

  while (1) {
    size_t message_length;
    // Read the length of the incoming message
    if (read(sock, &message_length, sizeof(size_t)) <= 0) {
      perror("- x - Erreur lors de la lecture de la longueur du message.\n");
      break;
    }

    // Allocate a buffer for the message
    char* buffer = malloc(message_length + 1);
    if (!buffer) {
      perror("- x - Échec de l'allocation mémoire.");
      break;
    }

    // Read the actual message
    int bytes_received = read(sock, buffer, message_length);
    if (bytes_received <= 0) {
      perror("- x - Erreur lors de la lecture du message.");
      free(buffer);
      break;
    }
    buffer[bytes_received] = '\0'; // Null-terminate the message

    printf("- i - Données reçues du client : %s\n", buffer);

    // Si le message commence par "#", c'est un message envoyé à un canal
    if (buffer[0] == '#') {
      // Trouver le canal correspondant
      char* channel_name = strtok(buffer, " ");
      Channel* channel = NULL;

      for (int i = 0; i < server_state.channel_count; i++) {
        if (strcmp(server_state.channels[i].name, channel_name) == 0) {
          channel = &server_state.channels[i];
          break;
        }
      }

      if (channel == NULL) {
        printf("- x - Canal non trouvé : %s\n", channel_name);
        free(buffer);
        continue;
      }

      // Extraire le message du buffer
      char* p = strtok(NULL, "");
      char* message = strdup(p);

      // Ajouter le message au canal
      pthread_mutex_lock(&clients_mutex);
      channel->messages = realloc(channel->messages, sizeof(Message) * (channel->message_count + 1));
      channel->messages[channel->message_count].text = message;
      channel->messages[channel->message_count].sender_fd = socket_index;
      channel->messages[channel->message_count].timestamp = time(NULL);
      channel->message_count++;
      pthread_mutex_unlock(&clients_mutex);

      // Envoyer le nouvel état à tous les clients
      broadcast_state(&server_state);

      // Enregistrer le nouvel état dans un fichier
      save_state_to_file(&server_state, "state.bin");
    }
    else if (buffer[0] == '+') { // Si le message commence par un +, on ajoute un channel

      // Si le premier charactère après le + n'est pas un #, on l'ajoute
      char* channel_name = buffer + 1;

      if (channel_name[0] != '#') {
        // On ajoute un # au début du nom du canal
        char* new_channel_name = malloc(strlen(channel_name) + 2);

        if (!new_channel_name) {
          perror("- x - Échec de l'allocation mémoire pour le nom du canal\n");
          free(buffer);
          continue;
        }

        new_channel_name[0] = '#';
        strcpy(new_channel_name + 1, channel_name);
        channel_name = new_channel_name;
      }

      // Vérifier si le canal existe déjà
      for (int i = 0; i < server_state.channel_count; i++) {
        if (strcmp(server_state.channels[i].name, channel_name) == 0) {
          printf("- x - Canal déjà existant : %s\n", channel_name);
          break;
        }
      }

      // Ajouter le canal
      pthread_mutex_lock(&clients_mutex);
      server_state.channels = realloc(server_state.channels, sizeof(Channel) * (server_state.channel_count + 1));
      server_state.channels[server_state.channel_count].name = channel_name;
      server_state.channels[server_state.channel_count].messages = malloc(sizeof(Message));
      char welcome_message[256];
      snprintf(welcome_message, sizeof(welcome_message), "Bienvenue sur le canal %s!", channel_name);
      server_state.channels[server_state.channel_count].messages[0].text = strdup(welcome_message);
      server_state.channels[server_state.channel_count].messages[0].sender_fd = -1;
      server_state.channels[server_state.channel_count].messages[0].timestamp = time(NULL);
      server_state.channels[server_state.channel_count].message_count = 1;
      server_state.channel_count++;
      pthread_mutex_unlock(&clients_mutex);

      // Envoyer le nouvel état à tous les clients
      broadcast_state(&server_state);

      // Enregistrer le nouvel état dans un fichier
      save_state_to_file(&server_state, "state.bin");
    }
    else if (buffer[0] == '.') { // Si le message commence par un ., c'est un fichier
      printf("- i - Fichier reçu\n");
      // Directement après le ., on a le channel
      // Trouver le canal correspondant
      char* channel_name = strtok(buffer, " ");
      Channel* channel = NULL;

      channel_name++; // On enlève le point

      printf("- i - Nom du canal : %s\n", channel_name);

      for (int i = 0; i < server_state.channel_count; i++) {
        if (strcmp(server_state.channels[i].name, channel_name) == 0) {
          channel = &server_state.channels[i];
          break;
        }
      }

      if (channel == NULL) {
        printf("- x - Canal non trouvé : %s\n", channel_name);
        free(buffer);
        continue;
      }

      printf("- i - Canal trouvé\n");


      // Puis on a la version sérialisée du fichier.
      char* file_buffer = strtok(NULL, "");

      // On désérialise le fichier
      MyFileStruct* file = deserialize_my_file_struct(file_buffer);

      if (!file) {
        printf("- x - Erreur lors de la désérialisation du fichier\n");
        free(buffer);
        continue;
      }

      printf("- i - Fichier désérialisé\n");
      printf("- i - Nom du fichier : %s\n", file->file_name);
      printf("- i - Contenu du fichier : %s\n", file->file_content);
      printf("- i - UUID du fichier : %s\n", file->uuid);

      // On ajoute le fichier à la liste des fichiers

      stored_files = realloc(stored_files, sizeof(MyFileStruct) * (stored_files_count + 1));
      stored_files[stored_files_count] = *file;
      stored_files_count++;

      printf("- i - Fichier ajouté à la liste\n");

      // On envoie un message au canal pour informer de l'ajout du fichier
      pthread_mutex_lock(&clients_mutex);

      // On réalloue la mémoire pour le nouveau message
      Message* new_messages = realloc(channel->messages, sizeof(Message) * (channel->message_count + 2));
      if (!new_messages) {
        perror("- x - Failed to reallocate messages array");
        pthread_mutex_unlock(&clients_mutex);
        free(buffer);
        continue;
      }
      channel->messages = new_messages;

      // On alloue la mémoire pour le texte du message
      channel->messages[channel->message_count].text = malloc(256);
      if (!channel->messages[channel->message_count].text) {
        perror("- x - Failed to allocate message text");
        pthread_mutex_unlock(&clients_mutex);
        free(buffer);
        continue;
      }

      // On formate le message
      snprintf(channel->messages[channel->message_count].text, 256,
        "\033[1;34ma envoyé un fichier :\033[0;0m %s", file->file_name);
      channel->messages[channel->message_count].sender_fd = socket_index;
      channel->messages[channel->message_count].timestamp = time(NULL);
      channel->message_count++;

      // On alloue la mémoire pour le texte du message
      channel->messages[channel->message_count].text = malloc(256);
      if (!channel->messages[channel->message_count].text) {
        perror("- x - Failed to allocate message text");
        pthread_mutex_unlock(&clients_mutex);
        free(buffer);
        continue;
      }

      // On formate le message
      snprintf(channel->messages[channel->message_count].text, 256,
        "Pour le télécharger, utilisez \033[1;34m/download %s\033[0;0m", file->uuid);
      channel->messages[channel->message_count].sender_fd = socket_index;
      channel->messages[channel->message_count].timestamp = time(NULL);
      channel->message_count++;

      pthread_mutex_unlock(&clients_mutex);

      printf("- i - Message ajouté au canal\n");

      // Envoyer le nouvel état à tous les clients
      broadcast_state(&server_state);

      // Enregistrer le nouvel état dans un fichier
      save_state_to_file(&server_state, "state.bin");

      printf("- i - Fichier ajouté avec succès\n");
    }
    else if (buffer[0] == '<') {
      // On a une demande de téléchargement de fichier.
      // On commence par extraire l'UUID du fichier
      char* uuid = buffer + 1;

      printf("- i - Demande de téléchargement du fichier : %s\n", uuid);

      // On cherche le fichier correspondant
      MyFileStruct* file = NULL;

      for (int i = 0; i < stored_files_count; i++) {
        if (strcmp(stored_files[i].uuid, uuid) == 0) {
          file = &stored_files[i];
          break;
        }
      }

      if (file == NULL) {
        printf("- x - Fichier non trouvé : %s\n", uuid);
        free(buffer);
        continue;
      }

      // On envoie le fichier au client
      size_t file_size;
      char* serialized_file = serialize_my_file_struct(file, &file_size);

      // On l'envoie au format ><serialized_file>
      size_t message_size = 1 + file_size + 1;

      // Allouer le buffer du message
      char* message_buffer = malloc(message_size);

      if (!message_buffer) {
        perror("- x - Erreur lors de l'allocation du tampon de message");
        free(serialized_file);
        free(buffer);
        continue;
      }

      char* p = message_buffer;

      // Ajouter le point de départ
      *p = '>';
      p += 1;

      // Copier le fichier sérialisé
      memcpy(p, serialized_file, file_size);

      // Envoyer la taille du message
      if (write(sock, &message_size, sizeof(size_t)) == -1) {
        perror("- x - Erreur lors de l'envoi de la taille du message");
        free(serialized_file);
        free(message_buffer);
        free(buffer);
        continue;
      }

      // Envoyer le message
      if (write(sock, message_buffer, message_size) == -1) {
        perror("- x - Erreur lors de l'envoi du message");
        free(serialized_file);
        free(message_buffer);
        free(buffer);
        continue;
      }

      free(serialized_file);
      free(message_buffer);
    }
    else {
      printf("- x - Commande non reconnue : %s\n", buffer);
    }

    free(buffer);
  }

  // Remove the client socket from the array
  client_sockets[socket_index] = -1;

  close(sock);
  pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
  signal(SIGINT, handle_sigint);

  // Initialiser le tableau des sockets clients
  for (int i = 0; i < MAX_CLIENTS; i++) {
    client_sockets[i] = -1;
  }

  // On vérifie si on a un état sauvegardé
  if (is_file_valid_state("state.bin")) {
    server_state = (*load_state_from_file("state.bin"));
  }
  else {
    // Initialiser l'état du serveur avec un canal par défaut
    server_state.channels = malloc(sizeof(Channel) * 2);

    server_state.channels[0].name = "#general";
    server_state.channels[0].messages = malloc(sizeof(Message) * 3);
    server_state.channels[0].messages[0].text = "Bienvenue sur le canal général !";
    server_state.channels[0].messages[0].timestamp = time(NULL);
    server_state.channels[0].messages[0].sender_fd = -1;
    server_state.channels[0].messages[1].text = "Vous pouvez désormais envoyer vos messages ici.";
    server_state.channels[0].messages[1].timestamp = time(NULL);
    server_state.channels[0].messages[1].sender_fd = -1;
    server_state.channels[0].messages[2].text = "Si vous avez des questions, n'hésitez pas!";
    server_state.channels[0].messages[2].timestamp = time(NULL);
    server_state.channels[0].messages[2].sender_fd = -1;
    server_state.channels[0].message_count = 3;

    server_state.channels[1].name = "#random";
    server_state.channels[1].messages = malloc(sizeof(Message) * 1);
    server_state.channels[1].messages[0].text = "Bienvenue sur le canal random!";
    server_state.channels[1].messages[0].timestamp = time(NULL);
    server_state.channels[1].messages[0].sender_fd = -1;
    server_state.channels[1].message_count = 1;

    server_state.channel_count = 2;
  }

  if (argc != 3) {
    fprintf(stderr, "- x - Usage: %s <Adresse IP> <Port>\n", argv[0]);
    exit(1);
  }

  const char* ip_address = argv[1];
  int port = atoi(argv[2]);

  // Étape 1 : Créer le socket du serveur
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    perror("- x - Erreur lors de la création du socket\n");
    exit(1);
  }

  // Étape 2 : Configurer l'adresse du serveur avec les arguments de la ligne de
  // commande
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port =
    htons(port); // Convertir le port en ordre d'octets réseau

  // Convertir l'adresse IP en format binaire et la stocker dans sin_addr
  if (inet_pton(AF_INET, ip_address, &server_address.sin_addr) <= 0) {
    perror("- x - Adresse invalide\n");
    close(server_socket);
    exit(1);
  }
  memset(server_address.sin_zero, '\0', sizeof(server_address.sin_zero));

  // Étape 3 : Lier le socket
  if (bind(server_socket, (struct sockaddr*)&server_address,
    sizeof(server_address)) == -1) {
    perror("- x - Erreur lors de la liaison du socket\n");
    close(server_socket);
    exit(1);
  }

  // Étape 4 : Écouter les connexions
  if (listen(server_socket, SOMAXCONN) == -1) {
    perror("Erreur lors de l'écoute sur le socket\n");
    close(server_socket);
    exit(1);
  }

  printf("- i - Serveur à l'écoute sur %s:%d...\n", ip_address, port);

  while (1) {
    // Étape 5 : Accepter une connexion client
    struct sockaddr_in client_address;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
    printf("- i - Connexion initiée avec le client %s:%d\n", client_ip,
      ntohs(client_address.sin_port));

    socklen_t client_size = sizeof(client_address);
    int* client_socket = malloc(sizeof(int));
    *client_socket =
      accept(server_socket, (struct sockaddr*)&client_address, &client_size);

    if (*client_socket == -1) {
      perror("- x - Erreur lors de l'acceptation de la connexion\n");
      free(client_socket);
      continue; // Continuer à accepter la prochaine connexion
    }

    // Étape 6 : Créer un thread pour gérer le client
    pthread_t client_thread;
    if (pthread_create(&client_thread, NULL, handle_client, client_socket) !=
      0) {
      perror("- x - Erreur lors de la création du thread\n");
      close(*client_socket);
      free(client_socket);
    }
    else {
      // Détacher le thread pour qu'il puisse se nettoyer après lui-même une
      // fois terminé
      printf("- √ - Thread client créé\n");
      pthread_detach(client_thread);
    }
  }

  // Étape 7 : Fermer les sockets
  // close(client_socket);
  close(server_socket);

  return 0;
}