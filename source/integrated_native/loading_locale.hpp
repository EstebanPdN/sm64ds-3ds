#pragma once
#include <cstdint>

namespace loading_screen {
enum class Language : uint8_t { English, Spanish, Portuguese, French, Italian, German };
enum class Stage : uint8_t { Audio, Ready };
enum class Error : uint8_t { Rom, Memory, Audio, Storage, Preparation, Cancelled, RomMissing, RomRevision };
enum class Label : uint8_t {
    Loading,Initial,Intro,VerifyRom,VerifyRomDetail,VerifyCache,VerifyCacheDetail,
    Extract,ExtractDetail,Finish,FinishDetail,Audio,Ready,ErrorLabel,Files,Cancel,Exit,LongWait,AlmostThere,Count
};
inline constexpr unsigned language_count=6;
inline constexpr Language normalize(Language language) {
    return unsigned(language)<language_count?language:Language::English;
}
inline constexpr Language from_system_language(unsigned value,bool init_ok=true,bool read_ok=true) {
    if(!init_ok||!read_ok)return Language::English;
    switch(value){case 2:return Language::French;case 3:return Language::German;case 4:return Language::Italian;
        case 5:return Language::Spanish;case 9:return Language::Portuguese;default:return Language::English;}
}
// The service opens one reference and balances it after either read outcome.
// Host tests exercise this exact lifetime path; the 3DS adapter calls CFGU.
template<class Service> Language query_language(Service& service) {
    if(!service.open())return Language::English;
    unsigned raw=255;const bool read_ok=service.read(raw);service.close();
    return from_system_language(raw,true,read_ok);
}
inline const char* language_tag(Language language) {
    static constexpr const char* tags[]={"en","es","pt","fr","it","de"};
    return tags[unsigned(normalize(language))];
}
inline const char* translated(Language language,Label label) {
    static constexpr const char* strings[language_count][unsigned(Label::Count)]={
        {"LOADING","PREPARING GAME","YOUR ADVENTURE WILL START SOON","CHECKING ROM","CHECKING YOUR GAME COPY.",
         "CHECKING FILES","CHECKING THE PREVIOUS SETUP.","PREPARING FILES","PREPARING THE GAME ON YOUR SD CARD.",
         "FINISHING FILES","SAVING THE SETUP ON YOUR SD CARD.","PREPARING AUDIO","READY TO PLAY","ERROR","FILES","B CANCEL","B / START: QUIT","This will take forever.","Almost there!"},
        {"CARGANDO","PREPARANDO JUEGO","TU AVENTURA ESTÁ POR COMENZAR","VERIFICANDO LA ROM","COMPROBANDO TU COPIA DEL JUEGO.",
         "COMPROBANDO ARCHIVOS","REVISANDO LA PREPARACIÓN ANTERIOR.","PREPARANDO ARCHIVOS","PREPARANDO EL JUEGO EN TU TARJETA SD.",
         "FINALIZANDO ARCHIVOS","GUARDANDO LA PREPARACIÓN EN LA SD.","PREPARANDO AUDIO","LISTO PARA JUGAR","ERROR","ARCHIVOS","B CANCELAR","B / START: SALIR","Va a demorar mucho.","Ya casi!"},
        {"CARREGANDO","PREPARANDO JOGO","SUA AVENTURA VAI COMEÇAR","VERIFICANDO A ROM","VERIFICANDO SUA CÓPIA DO JOGO.",
         "VERIFICANDO ARQUIVOS","VERIFICANDO A PREPARAÇÃO ANTERIOR.","PREPARANDO ARQUIVOS","PREPARANDO O JOGO NO SEU CARTÃO SD.",
         "FINALIZANDO ARQUIVOS","SALVANDO A PREPARAÇÃO NO CARTÃO SD.","PREPARANDO ÁUDIO","PRONTO PARA JOGAR","ERRO","ARQUIVOS","B CANCELAR","B / START: SAIR","Vai demorar bastante.","Já quase!"},
        {"CHARGEMENT","PRÉPARATION DU JEU","TON AVENTURE VA COMMENCER","VÉRIFICATION ROM","VÉRIFICATION DE TA COPIE DU JEU.",
         "VÉRIFICATION FICHIERS","VÉRIFICATION DES DONNÉES EXISTANTES.","PRÉPARATION FICHIERS","PRÉPARATION DU JEU SUR TA CARTE SD.",
         "FINALISATION FICHIERS","ENREGISTREMENT DES DONNÉES SUR LA SD.","PRÉPARATION AUDIO","PRÊT À JOUER","ERREUR","FICHIERS","B ANNULER","B / START: QUITTER","Ça va prendre un bon moment.","On y est presque !"},
        {"CARICAMENTO","PREPARAZIONE GIOCO","LA TUA AVVENTURA STA PER INIZIARE","VERIFICA ROM","VERIFICA DELLA TUA COPIA DEL GIOCO.",
         "VERIFICA FILE","VERIFICA DELLA PREPARAZIONE PRECEDENTE.","PREPARAZIONE FILE","PREPARAZIONE DEL GIOCO SULLA SCHEDA SD.",
         "FINALIZZAZIONE FILE","SALVATAGGIO DEI DATI SULLA SCHEDA SD.","PREPARAZIONE AUDIO","PRONTO A GIOCARE","ERRORE","FILE","B ANNULLA","B / START: ESCI","Ci vorrà un bel po'.","Ci siamo quasi!"},
        {"LADEN","SPIEL VORBEREITEN","DEIN ABENTEUER BEGINNT BALD","ROM WIRD GEPRÜFT","DEINE SPIELDATEI WIRD GEPRÜFT.",
         "DATEIEN PRÜFEN","VORHANDENE DATEN WERDEN GEPRÜFT.","DATEIEN VORBEREITEN","DAS SPIEL WIRD AUF DER SD VORBEREITET.",
         "DATEIEN ABSCHLIESSEN","DIE DATEN WERDEN AUF DER SD GESPEICHERT.","AUDIO VORBEREITEN","BEREIT ZUM SPIELEN","FEHLER","DATEIEN","B ABBRECHEN","B / START: BEENDEN","Das dauert eine ganze Weile.","Fast geschafft!"}
    };
    const unsigned index=unsigned(label)<unsigned(Label::Count)?unsigned(label):unsigned(Label::Initial);
    return strings[unsigned(normalize(language))][index];
}
inline const char* error_title(Language language,Error error) {
    static constexpr const char* titles[language_count][8]={
        {"ROM SETUP FAILED","NOT ENOUGH MEMORY","AUDIO SETUP FAILED","SD CARD ERROR","SETUP FAILED","CANCELLED","ROM NOT FOUND","WRONG ROM VERSION"},
        {"ERROR CON LA ROM","MEMORIA INSUFICIENTE","ERROR DE AUDIO","ERROR DE TARJETA SD","ERROR DE PREPARACIÓN","CANCELADO","ROM NO ENCONTRADA","ROM INCORRECTA"},
        {"ERRO COM A ROM","MEMÓRIA INSUFICIENTE","ERRO DE ÁUDIO","ERRO NO CARTÃO SD","ERRO NA PREPARAÇÃO","CANCELADO","ROM NÃO ENCONTRADA","ROM INCORRETA"},
        {"ERREUR DE ROM","MÉMOIRE INSUFFISANTE","ERREUR AUDIO","ERREUR DE CARTE SD","ÉCHEC DE PRÉPARATION","ANNULÉ","ROM INTROUVABLE","ROM INCOMPATIBLE"},
        {"ERRORE DELLA ROM","MEMORIA INSUFFICIENTE","ERRORE AUDIO","ERRORE SCHEDA SD","PREPARAZIONE FALLITA","ANNULLATO","ROM NON TROVATA","ROM ERRATA"},
        {"ROM-FEHLER","ZU WENIG SPEICHER","AUDIO-FEHLER","SD-KARTENFEHLER","VORBEREITUNG FEHLGESCHLAGEN","ABGEBROCHEN","ROM NICHT GEFUNDEN","FALSCHE ROM-VERSION"}
    };
    return titles[unsigned(normalize(language))][unsigned(error)<8?unsigned(error):unsigned(Error::Preparation)];
}
inline const char* error_help(Language language,Error error) {
    static constexpr const char* help[language_count][8]={
        {"Put your original EU .nds ROM in sdmc:/3ds/sm64ds/.","Restart the game to free memory, then try again.","Restart the game. Check the log if audio setup fails again.","Check your SD card and its free space, then try again.","Check your ROM and SD card, then try again.","You can restart the game to continue setup.","Put your original EU .nds ROM in sdmc:/3ds/sm64ds/.","The original EU ROM is required. Check your copy."},
        {"Pon tu ROM europea original .nds en sdmc:/3ds/sm64ds/.","Reinicia el juego para liberar memoria e inténtalo de nuevo.","Reinicia el juego. Revisa el registro si el audio vuelve a fallar.","Comprueba la tarjeta SD y su espacio libre e inténtalo de nuevo.","Comprueba tu ROM y la tarjeta SD e inténtalo de nuevo.","Puedes reiniciar el juego para continuar la preparación.","Pon tu ROM europea original .nds en sdmc:/3ds/sm64ds/.","Se necesita la ROM europea original. Comprueba tu copia."},
        {"Coloque sua ROM europeia original .nds em sdmc:/3ds/sm64ds/.","Reinicie o jogo para liberar memória e tente novamente.","Reinicie o jogo. Confira o registro se o áudio falhar de novo.","Confira o cartão SD e seu espaço livre e tente novamente.","Confira sua ROM e o cartão SD e tente novamente.","Reinicie o jogo para continuar a preparação.","Coloque sua ROM europeia original .nds em sdmc:/3ds/sm64ds/.","É necessária a ROM europeia original. Confira sua cópia."},
        {"Place ta ROM européenne originale .nds dans sdmc:/3ds/sm64ds/.","Relance le jeu pour libérer de la mémoire, puis réessaie.","Relance le jeu. Consulte le journal si l'audio échoue encore.","Vérifie la carte SD et son espace libre, puis réessaie.","Vérifie ta ROM et ta carte SD, puis réessaie.","Relance le jeu pour continuer la préparation.","Place ta ROM européenne originale .nds dans sdmc:/3ds/sm64ds/.","La ROM européenne originale est requise. Vérifie ta copie."},
        {"Metti la ROM europea originale .nds in sdmc:/3ds/sm64ds/.","Riavvia il gioco per liberare memoria e riprova.","Riavvia il gioco. Controlla il registro se l'audio fallisce ancora.","Controlla la scheda SD e lo spazio libero, poi riprova.","Controlla la ROM e la scheda SD, poi riprova.","Puoi riavviare il gioco per continuare la preparazione.","Metti la ROM europea originale .nds in sdmc:/3ds/sm64ds/.","È necessaria la ROM europea originale. Controlla la tua copia."},
        {"Lege deine originale EU-ROM als .nds in sdmc:/3ds/sm64ds/.","Starte das Spiel neu, um Speicher freizugeben, und versuche es erneut.","Starte das Spiel neu. Prüfe bei erneutem Audio-Fehler das Protokoll.","Prüfe die SD-Karte und den freien Speicher und versuche es erneut.","Prüfe deine ROM und die SD-Karte und versuche es erneut.","Starte das Spiel neu, um die Vorbereitung fortzusetzen.","Lege deine originale EU-ROM als .nds in sdmc:/3ds/sm64ds/.","Die originale EU-ROM wird benötigt. Prüfe deine Kopie."}
    };
    return help[unsigned(normalize(language))][unsigned(error)<8?unsigned(error):unsigned(Error::Preparation)];
}
// Decode UTF-8 into the original game's Unicode Latin1-indexed atlas. Invalid
// or unsupported input becomes one '?' glyph, never separate continuation bytes.
inline uint8_t next_latin1(const char*& text) {
    const auto first=static_cast<uint8_t>(*text);
    if(!first)return 0;
    ++text;
    if(first<0x80)return first;
    if((first==0xc2||first==0xc3)&&*text){const auto second=static_cast<uint8_t>(*text);
        if((second&0xc0)==0x80){++text;return uint8_t(((first&3u)<<6)|(second&63u));}}
    unsigned remaining=first>=0xf0&&first<=0xf4?3u:first>=0xe0&&first<=0xef?2u:first>=0xc0&&first<=0xdf?1u:0u;
    while(remaining&&*text&&(static_cast<uint8_t>(*text)&0xc0)==0x80){++text;--remaining;}
    return '?';
}
} // namespace loading_screen
