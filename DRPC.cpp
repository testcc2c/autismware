#include "includes.h"
#include "discord_register.h"
#include "discord_rpc.h"

DRPC g_drpc{ };;

void DRPC::Start() {
    DiscordEventHandlers Handler;
    memset(&Handler, 0, sizeof(Handler));
    Discord_Initialize("987866107010506782", &Handler, 1, NULL);
}

void DRPC::Update() {
    DiscordRichPresence discordPresence;
    memset(&discordPresence, 0, sizeof(discordPresence));
    discordPresence.state = "Playing Legacy CS";
    discordPresence.startTimestamp = 0;
    discordPresence.largeImageText = "Autismware";
    discordPresence.largeImageKey = "https://i.imgur.com/4wJuoq1.jpeg";
    Discord_UpdatePresence(&discordPresence);
}