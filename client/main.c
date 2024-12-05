#include <arpa/inet.h>  // Pour inet_pton()
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "../state.c"

#define PRINT_USER_INPUT_SIGNAL 10

Channel* selected_channel = NULL;

State* current_state = NULL;

volatile char* user_input;


pthread_t state_thread;
pthread_t input_thread;

/**
 * @brief Affiche l'interface du client.
 *
 * @param state {State*} l'état à afficher
 * @return {void}
 */
void display_state(State* state) {
  // Effacer l'écran
  printf("\033[H\033[J");

  // Calculer la hauteur et la largeur du terminal
  struct winsize w;
  ioctl(0, TIOCGWINSZ, &w);

  int terminal_height = w.ws_row;
  int terminal_width = w.ws_col;

  // Pour faciliter le calcul de l'interface, on va calculer séparément l'affichage des messages et des canaux

  // On commence par calculer les canaux
  // Les cannux sont affichés à gauche de l'écran sur 20 caractères maximum pour
  // chaque ligne disponible
  char** channel_lines = malloc(terminal_height * sizeof(char*));
  if (!channel_lines) {
    perror("- x - Erreur lors de l'allocation de la mémoire pour les lignes de canal");
    exit(1);
  }

  for (int i = 0; i < terminal_height; i++) {
    // Allouer 21 caractères pour chaque ligne de canal
    channel_lines[i] = malloc(21 * sizeof(char));
    if (!channel_lines[i]) {
      perror("- x - Erreur lors de l'allocation de la mémoire pour une ligne de canal");
      exit(1);
    }
    memset(channel_lines[i], ' ', 20);
    channel_lines[i][20] = '\0';

    // Afficher le nom du canal
    if (i < state->channel_count) {
      strncpy(channel_lines[i], state->channels[i].name, strlen(state->channels[i].name));
    }

    // Si le canal est sélectionné, on affiche un chevron à gauche
    if (&state->channels[i] == selected_channel) {
      channel_lines[i][0] = '>';
    }
  }

  // On continue avec les messages
  // Les messages sont affichés à droite de l'écran
  // On affiche les messages du canal sélectionné
  // Les messages sont affichés sur toutes les lignes
  // sauf les 2 dernières qui sont réservées pour la saisie de texte
  // Ils sont affichés sur toute la largeur de l'écran moins 21 caractères

  int message_lines_count = terminal_height - 2;
  char** message_lines = malloc(message_lines_count * sizeof(char*));
  if (!message_lines) {
    perror("- x - Erreur lors de l'allocation de la mémoire pour les lignes de message");
    exit(1);
  }

  int remaining_width = terminal_width - 21; // Éviter de calculer à chaque fois

  for (int i = 0; i < message_lines_count; i++) {
    // Allouer (terminal_width - 21) caractères pour chaque ligne de message
    message_lines[i] = malloc((remaining_width) * sizeof(char));
    if (!message_lines[i]) {
      perror("- x - Erreur lors de l'allocation de la mémoire pour une ligne de message");
      exit(1);
    }
    memset(message_lines[i], ' ', remaining_width);
    message_lines[i][remaining_width] = '\0';
  }

  // On vient remplir les lignes de message en commençant par les messages les plus récents
  // On parcours les lignes en partant du bas, car les messages les plus récents sont en bas
  // On prend en compte qu'un message peut être plus long que la largeur de l'écran
  // Il prendra alors plusieurs lignes

  if (selected_channel != NULL) {
    // On commence par couper les messages en plusieurs lignes.
    char** messages_to_display = malloc(message_lines_count * sizeof(char*));
    if (!messages_to_display) {
      perror("- x - Erreur lors de l'allocation de la mémoire pour les messages à afficher");
      exit(1);
    }

    for (int i = 0; i < message_lines_count; i++) {
      messages_to_display[i] = malloc(remaining_width * sizeof(char));
      if (!messages_to_display[i]) {
        perror("- x - Erreur lors de l'allocation de la mémoire pour un message à afficher");
        exit(1);
      }

      // On remplit les lignes avec des espaces
      memset(messages_to_display[i], ' ', remaining_width);
    }

    int messages_to_display_index = message_lines_count - 1;

    // On va passer sur chaque message depuis la fin pour le découper en plusieurs lignes
    for (int i = selected_channel->message_count - 1; i >= 0; i--) {
      // On prend le message, on compte le nombre de lignes nécessaires pour l'afficher
      // On découpe le message en plusieurs lignes et on les stocke dans messages_to_display
      Message* message = &selected_channel->messages[i];
      char* text = message->text;

      int text_length = strlen(text);

      time_t t = message->timestamp;

      struct tm* tmp = gmtime(&t);

      int h = (tmp->tm_hour + 1) % 24; // On ajoute 1 pour passer de GMT à Paris
      int m = tmp->tm_min;
      int s = tmp->tm_sec;

      // On prend en compte le nom de l'utilisateur (user (hh:mm:ss) >)
      char user[40];
      if (message->sender_fd == -1) {
        snprintf(user, 40, "\033[1;31mServer (%02d:%02d:%02d) >\033[0;0m ", h, m, s);
      }
      else {
        snprintf(user, 40, "\033[1;33mUser%d (%02d:%02d:%02d) >\033[0;0m ", message->sender_fd, h, m, s);
      }

      int user_length = strlen(user);
      int total_length = text_length + user_length;

      // On calcule le nombre de lignes nécessaires pour afficher le message
      int lines_needed = (total_length + remaining_width - 1) / remaining_width;

      // On découpe le message en plusieurs lignes en démarrant du début
      int remaining_text_length = text_length;
      char* text_ptr = text;

      // Stocker les lignes temporairement pour les inverser ensuite
      char** temp_lines = malloc(lines_needed * sizeof(char*));
      for (int j = 0; j < lines_needed; j++) {
        temp_lines[j] = malloc(remaining_width * sizeof(char));
        memset(temp_lines[j], ' ', remaining_width);
      }

      for (int j = 0; j < lines_needed; j++) {
        // On calcule la longueur du texte à copier sur la ligne actuelle
        int available_space = (j == 0) ? (remaining_width - user_length) : remaining_width;
        int copy_length = remaining_text_length > available_space ? available_space : remaining_text_length;

        // On copie le texte sur la ligne actuelle
        if (j == 0) {
          // On commence par afficher le nom de l'utilisateur
          strncpy(temp_lines[j], user, user_length);
          strncpy(temp_lines[j] + user_length, text_ptr, copy_length);
        }
        else {
          strncpy(temp_lines[j], text_ptr, copy_length);
        }

        // On met à jour les pointeurs et les compteurs
        remaining_text_length -= copy_length;
        text_ptr += copy_length;
      }

      // Inverser les lignes et les copier dans messages_to_display
      for (int j = lines_needed - 1; j >= 0; j--) {
        strncpy(messages_to_display[messages_to_display_index], temp_lines[j], remaining_width);
        messages_to_display_index--;

        // Si on a atteint la première ligne, on arrête
        if (messages_to_display_index < 0) {
          break;
        }
      }

      // Libérer la mémoire temporaire
      for (int j = 0; j < lines_needed; j++) {
        free(temp_lines[j]);
      }
      free(temp_lines);
    }

    // On remplit les lignes de message avec les messages découpés
    for (int i = 0; i < message_lines_count; i++) {
      strncpy(message_lines[i], messages_to_display[i], remaining_width);
    }
  }

  // On affiche les canaux et les messages
  for (int i = 0; i < terminal_height - 1; i++) {
    if (i < message_lines_count) {
      printf("%s│%s\n", channel_lines[i], message_lines[i]);
    }
    else {
      printf("%s│\n", channel_lines[i]);
    }
  }

  // La dernière ligne est réservée pour la saisie de texte
  // On affiche tout de même le séparateur
  printf("                    │ > ");

  pthread_kill(input_thread, PRINT_USER_INPUT_SIGNAL);

  // On force l'affichage, car on a utilisé printf sans saut de ligne
  fflush(stdout);
}


/**
 * @brief Écoute sur le socket pour recevoir les nouveaux
 *        états envoyés par le serveur, puis utilise display_state
 *        pour l'afficher.
 *
 * @param arg {int*} Le socket de connexion au serveur
 * @return {void*}
 */
void* listen_for_new_state(void* arg) {
  int client_socket = *(int*)arg;

  while (1) {
    size_t size;
    if (read(client_socket, &size, sizeof(size_t)) == -1) {
      perror("- x - Erreur lors de la lecture de la taille de l'état");
      close(client_socket);
      exit(1);
    }

    char* buffer = malloc(size);
    if (!buffer) {
      perror("- x - Erreur lors de l'allocation du tampon");
      close(client_socket);
      exit(1);
    }

    if (read(client_socket, buffer, size) == -1) {
      perror("- x - Erreur lors de la lecture de l'état");
      close(client_socket);
      exit(1);
    }

    printf("- √ - État reçu\n");
    State* state = deserialize_state(buffer);

    if (selected_channel == NULL) {
      selected_channel = &state->channels[0];
    }

    // On resélectionne le canal si possible
    for (int i = 0; i < state->channel_count; i++) {
      if (strcmp(state->channels[i].name, selected_channel->name) == 0) {
        selected_channel = &state->channels[i];
        break;
      }
    }

    free(current_state);

    current_state = state;

    display_state(state);

    free(buffer);
  }
}

void print_user_input(int signal) {
  char* buffer = strdup((const char*)user_input);
  printf("%s", strlen(buffer) > 0 ? buffer : "(vide)");
  fflush(stdout);
}

/**
 * @brief Gère l'entrée utilisateur dans un thread.
 *        Gère également l'envoi de messages et de commandes.
 *
 * @param arg {int*} Le socket de connexion au serveur
 * @return {void*}
 */
void* expect_user_input(void* arg) {
  int client_socket = *(int*)arg;

  // Initialiser le user_input
  user_input = malloc(2 * sizeof(char));
  user_input[0] = 'a';
  user_input[1] = '\0';

  // On lie le signal à la fonction d'affichage
  signal(PRINT_USER_INPUT_SIGNAL, print_user_input);

  while (1) {
    while (1) {
      // On récupère charactère par charactère
      char c = getchar();

      if (c == '\n') {
        if (strlen(user_input) > 0) {
          break;
        }
        else {
          continue;
        }
      }

      // On ajoute le charactère au global user_input
      size_t user_input_length = strlen((const char*)user_input);
      user_input = realloc((void*)user_input, user_input_length + 2);
      if (!user_input) {
        perror("- x - Erreur lors de la réallocation de la mémoire pour la saisie utilisateur");
        close(client_socket);
        exit(1);
      }
      user_input[user_input_length] = c;
      user_input[user_input_length + 1] = '\0';
    }

    char* buffer = strdup((const char*)user_input);

    // Enlever le saut de ligne
    buffer[strcspn(buffer, "\n")] = 0;

    // Si le message commence par "/", c'est une commande
    if (buffer[0] == '/') {
      if (strcmp(buffer, "/exit") == 0) {
        free(buffer);
        close(client_socket);
        exit(0);
      }
      else if (strncmp(buffer, "/switch ", 8) == 0) {
        char* channel_name = buffer + 8;

        // Trouver le canal correspondant
        if (current_state != NULL) {
          for (int i = 0; i < current_state->channel_count; i++) {
            if (strcmp(current_state->channels[i].name, channel_name) == 0) {
              selected_channel = &current_state->channels[i];
              break;
            }
          }
        }
      }
      else if (strncmp(buffer, "/create ", 8) == 0) {
        char* channel_name = buffer + 8;

        // On commence par sérialiser la commande sous la forme "+<channel_name>"
        size_t message_size = 1 + strlen(channel_name) + 1;

        char* message_buffer = malloc(message_size);

        if (!message_buffer) {
          perror("- x - Erreur lors de l'allocation du tampon de message");
          close(client_socket);
          exit(1);
        }

        snprintf(message_buffer, message_size, "+%s", channel_name);

        // Envoyer la taille du message

        size_t size = message_size;

        if (write(client_socket, &size, sizeof(size_t)) == -1) {
          perror("- x - Erreur lors de l'envoi de la taille du message");
          close(client_socket);
          exit(1);
        }

        printf("- √ - Taille envoyée\n");

        // Envoyer le message

        if (write(client_socket, message_buffer, message_size) == -1) {
          perror("- x - Erreur lors de l'envoi du message");
          close(client_socket);
          exit(1);
        }

        printf("- √ - Message envoyé\n");
        free(message_buffer);
      }
    }
    else {
      // Dans le cas contraire, on envoi le message au serveur
      // On commence par sérialiser le message
      // Sous la forme <channel_name> <message> (on ne peut pas avoir d'espace dans le nom du canal)

      if (selected_channel == NULL) {
        printf("- x - Aucun canal sélectionné\n");
        free(buffer);
        continue;
      }

      size_t message_size = strlen(selected_channel->name) + 1 + strlen(buffer) + 1;

      char* message_buffer = malloc(message_size);

      if (!message_buffer) {
        perror("- x - Erreur lors de l'allocation du tampon de message");
        close(client_socket);
        exit(1);
      }

      snprintf(message_buffer, message_size, "%s %s", selected_channel->name, buffer);

      // Envoyer la taille du message
      size_t size = message_size;
      if (write(client_socket, &size, sizeof(size_t)) == -1) {
        perror("- x - Erreur lors de l'envoi de la taille du message");
        close(client_socket);
        exit(1);
      }

      printf("- √ - Taille envoyée\n");

      // Envoyer le message
      if (write(client_socket, message_buffer, message_size) == -1) {
        perror("- x - Erreur lors de l'envoi du message");
        close(client_socket);
        exit(1);
      }

      printf("- √ - Message envoyé\n");

      free(message_buffer);
    }

    free(buffer);

    free((void*)user_input);
    user_input = malloc(1 * sizeof(char));
    user_input[0] = '\0';

    display_state(current_state);
  }
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "- x - Usage: %s <Adresse IP> <Port>\n", argv[0]);
    exit(1);
  }

  const char* ip_address = argv[1];
  int port = atoi(argv[2]);

  // Étape 1 : Créer le socket client
  int client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (client_socket == -1) {
    perror("- x - Erreur lors de la création du socket");
    exit(1);
  }

  // Étape 2 : Configurer l'adresse du serveur avec les arguments de la ligne de
  // commande
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port =
    htons(port);  // Convertir le port en ordre d'octets réseau

  // Convertir l'adresse IP en format binaire et la stocker dans sin_addr
  if (inet_pton(AF_INET, ip_address, &server_address.sin_addr) <= 0) {
    perror("- x - Adresse invalide");
    close(client_socket);
    exit(1);
  }

  // Étape 3 : Se connecter au serveur
  if (connect(client_socket, (struct sockaddr*)&server_address,
    sizeof(server_address)) == -1) {
    perror("- x - Erreur lors de la connexion au serveur");
    close(client_socket);
    exit(1);
  }

  // Étape 4 : Lancer un thread pour écouter les mises à jour de l'état
  pthread_create(&state_thread, NULL, listen_for_new_state, &client_socket);

  // Étape 5 : Lancer un thread pour attendre les saisies de l'utilisateur
  pthread_create(&input_thread, NULL, expect_user_input, &client_socket);

  pthread_join(state_thread, NULL);
  pthread_join(input_thread, NULL);

  // Étape 6 : Fermer le socket
  close(client_socket);

  return 0;
}
