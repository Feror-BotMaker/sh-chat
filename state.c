#ifndef STATE_C
#define STATE_C
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct message_t {
    char* text;
    int sender_fd;
    time_t timestamp;
} Message;

typedef struct channel_t {
    char* name;
    Message* messages;
    int message_count;
} Channel;

typedef struct user_t {
    char* username;
    int socket_fd;
} User;

typedef struct state_t {
    Channel* channels;
    int channel_count;
    User* users;
    int user_count;
} State;

/**
 * @brief Sérialise un struct State vers une chaine de caractères
 *        pour pouvoir l'envoyer par la suite via une connexion TCP.
 *
 * @param state {State*} L'état à sérialiser
 * @param size_out {size_t*} La taille du buffer produit (pour l'envoi)
 * @return {char*} La version sérialisé de l'état
 */
char* serialize_state(const State* state, size_t* size_out) {

    // Étape 1: On commence par calculer l'espace à alouer pour stocker l'état
    size_t size = sizeof(int); // Pour stocker `channel_count`
    for (int i = 0; i < state->channel_count; i++) {
        Channel* channel = &state->channels[i];
        size += strlen(channel->name) + 1 + sizeof(int); // nom du channel + null(\0) + nombre de messages
        for (int j = 0; j < channel->message_count; j++) {
            Message* message = &channel->messages[j];
            size += strlen(message->text) + 1 + sizeof(int) + sizeof(time_t);
            // ↑ Contenu du message + null(\0) + id de l'envoyeur (indice du socket) + heure d'envoi
        }
    }

    size += sizeof(int); // Pour stocker `user_count`
    for (int i = 0; i < state->user_count; i++) {
        User* user = &state->users[i];
        size += strlen(user->username) + 1 + sizeof(int); // nom d'utilisateur + null(\0) + socket_fd
    }

    char* buffer = malloc(size);
    if (!buffer) return NULL;

    char* p = buffer;

    // On sérialise le compte du nombre de channels
    memcpy(p, &state->channel_count, sizeof(int));
    p += sizeof(int); // Puis on décale de la taille d'un entier

    // Pour chaque channel
    for (int i = 0; i < state->channel_count; i++) {
        Channel* channel = &state->channels[i];

        // On sérialise le nom du channel
        size_t name_len = strlen(channel->name) + 1;
        memcpy(p, channel->name, name_len);
        p += name_len;

        // Puis le nombre de messages du channel
        memcpy(p, &channel->message_count, sizeof(int));
        p += sizeof(int);

        // Puis pour chaque message du channel
        for (int j = 0; j < channel->message_count; j++) {
            Message* message = &channel->messages[j];

            // On sérialise son contenu
            size_t text_len = strlen(message->text) + 1;
            memcpy(p, message->text, text_len);
            p += text_len;

            // Puis l'id de son envoyeur (indice du socket)
            memcpy(p, &message->sender_fd, sizeof(int));
            p += sizeof(int);

            // Et enfin l'heure d'envoi
            memcpy(p, &message->timestamp, sizeof(time_t));
            p += sizeof(time_t);
        }
    }

    // On sérialise le compte du nombre d'utilisateurs
    memcpy(p, &state->user_count, sizeof(int));
    p += sizeof(int); // Puis on décale de la taille d'un entier

    // Pour chaque utilisateur
    for (int i = 0; i < state->user_count; i++) {
        User* user = &state->users[i];

        // On sérialise le nom d'utilisateur
        size_t username_len = strlen(user->username) + 1;
        memcpy(p, user->username, username_len);
        p += username_len;

        // Puis le socket_fd de l'utilisateur
        memcpy(p, &user->socket_fd, sizeof(int));
        p += sizeof(int);
    }

    // On stock la taille totale du buffer dans la variable en sortie (pour connaître la taille à envoyer)
    *size_out = size;

    // Et enfin on renvoie le buffer.
    return buffer;
}

/**
 * @brief Désérialise un état depuis une chaîne de caractères
 *
 * @param buffer {char*} La chaine à désérialiser (doit contenir un état)
 * @return {State*} L'état du serveur
 */
State* deserialize_state(const char* buffer) {
    const char* p = buffer;

    // On déserialise le nombre de channels
    int channel_count;
    memcpy(&channel_count, p, sizeof(int));
    p += sizeof(int);

    // On alloue le nombre de channels adéquat.
    Channel* channels = malloc(channel_count * sizeof(Channel));
    if (!channels) return NULL;

    // Puis pour chaque channel
    for (int i = 0; i < channel_count; i++) {
        // On déserialise le nom du channel
        size_t name_len = strlen(p) + 1; // +1 pour \0
        channels[i].name = malloc(name_len);
        strcpy(channels[i].name, p);
        p += name_len;

        // Puis on désérialise le nombre de messages
        int message_count;
        memcpy(&message_count, p, sizeof(int));
        p += sizeof(int);
        channels[i].message_count = message_count;

        // On alloue le nombre de messages adéquat.
        channels[i].messages = malloc(message_count * sizeof(Message));

        // Puis pour chaque message
        for (int j = 0; j < message_count; j++) {
            // On déserialise le contenu du message
            size_t text_len = strlen(p) + 1; // +1 pour \0
            channels[i].messages[j].text = malloc(text_len);
            strcpy(channels[i].messages[j].text, p);
            p += text_len;

            // Puis l'id de son envoyeur (indice du socket)
            memcpy(&channels[i].messages[j].sender_fd, p, sizeof(int));
            p += sizeof(int);

            // Et enfin l'heure d'envoi
            memcpy(&channels[i].messages[j].timestamp, p, sizeof(time_t));
            p += sizeof(time_t);
        }
    }

    // On déserialise le nombre d'utilisateurs
    int user_count;
    memcpy(&user_count, p, sizeof(int));
    p += sizeof(int);

    // On alloue le nombre d'utilisateurs adéquat.
    User* users = malloc(user_count * sizeof(User));
    if (!users) return NULL;

    // Puis pour chaque utilisateur
    for (int i = 0; i < user_count; i++) {
        // On déserialise le nom d'utilisateur
        size_t username_len = strlen(p) + 1; // +1 pour \0
        users[i].username = malloc(username_len);
        strcpy(users[i].username, p);
        p += username_len;

        // Puis on déserialise le socket_fd de l'utilisateur
        memcpy(&users[i].socket_fd, p, sizeof(int));
        p += sizeof(int);
    }

    // Finalement, on alloue un peu de mémoire pour l'état
    State* state = malloc(sizeof(State));
    state->channels = channels;
    state->channel_count = channel_count;
    state->users = users;
    state->user_count = user_count;

    // Et on le renvoie
    return state;
}

/**
 * @brief Sauvegarde l'état du serveur dans un fichier
 *
 * @param state {State*} L'état à sauvegarder
 * @param filename {char*} Le nom du fichier où le sauvegarder
 * @return {void}
 */
void save_state_to_file(const State* state, const char* filename) {
    size_t size;
    char* buffer = serialize_state(state, &size);
    if (!buffer) return;

    FILE* file = fopen(filename, "wb");
    if (!file) {
        free(buffer);
        return;
    }

    fwrite(buffer, 1, size, file);
    fclose(file);
    free(buffer);
}

/**
 * @brief Vérifie si le fichier existe
 *
 * @param filename {char*} Le nom du fichier dont l'existance est remise en cause.
 * @return {short} 1 si le fichier existe, 0 sinon.
 */
short is_file_valid_state(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return 0;

    fclose(file);
    return 1;
}

/**
 * @brief Charge un état depuis un fichier.
 *
 * @param filename {char*}
 * @return {State*} L'état chargé
 */
State* load_state_from_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) return NULL;

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* buffer = malloc(size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    fread(buffer, 1, size, file);
    fclose(file);

    State* state = deserialize_state(buffer);
    free(buffer);

    return state;
}
#endif // ndef STATE_C