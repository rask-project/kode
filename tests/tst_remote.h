#pragma once

#include <QObject>

// kode-remote's frames, and Kode talking to a locally built agent: over
// its stdio (serve), and through the bridge and daemon.
class TestRemote : public QObject
{
    Q_OBJECT
private slots:
    void readsFramesArrivingInPieces();
    void refusesOversizedAndUnknownFrames();
    void handshakes();
    void answersRequests();
    void refusesRequestsBeforeInitialize();
    // A broken frame ends the agent's session.
    void endsOnABrokenFrame();
    // Pending requests fail when the connection is closed.
    void failsPendingRequestsOnClose();
    // Two connections through the bridge reach one daemon, as separate sessions.
    void bridgesToTheDaemon();
    // A runtime folder others can enter is refused.
    void refusesAnOpenRuntimeFolder();

    void validatesDestinations();
    void quotesForSh();
    void parsesTheProbe();
    void readsConfiguredHosts();
    // Through a stand-in ssh running the commands here: the agent installed
    // (0700), then found by its hash; changed, installed again.
    void installsTheAgent();
    void reinstallsAChangedAgent();
    void failsWithoutAnAgentForThePlatform();
    // Authentication refused: failed, not retried.
    void failsOnAPermanentError();
    // Online, then the daemon dies: offline, retried, online again.
    void reconnectsAfterALoss();
    // kode-askpass asks Hosts with its token: answered, cancelled; a wrong token is refused.
    void answersAskpass();
    void refusesAskpassWithAWrongToken();
    void tellsPromptKinds();

    // fs over serve: written (folders made) and read back, hashes alike.
    void writesAndReadsFiles();
    // Past the channels' first credit, both ways.
    void movesLargeFiles();
    // A file changed since it was read: refused, its version now sent, the file kept.
    void refusesAConflictingWrite();
    // Touched but not changed: the bytes' hash tells it is the same.
    void writesATouchedFile();
    void keepsTheModeAndWritesThroughLinks();
    void writesInPlaceInAReadOnlyFolder();
    void listsAFolder();
    void reportsAMissingFile();

    // Document with an ssh:// file, through a stand-in ssh and the agent.
    void opensAndSavesARemoteDocument();
    void keepsARemoteDocumentsEncoding();
    // Changed on the host since it was read: the save is refused and told;
    // Keep Mine, then it saves.
    void refusesAConflictingSave();
    // An edit made while a save is on its way stays modified.
    void keepsEditsMadeDuringASave();
    // Changed on the host while unmodified here: read again (polling).
    void reloadsARemoteChange();
    // A file that does not exist yet: empty, written (with its folders) when saved.
    void startsAMissingRemoteFile();

    // watch over serve: a watched folder's new entry, a watched file
    // changed in place and replaced by renaming, each told.
    void tellsWatchedChanges();
    // search/files: the folder's files, .git, build, node_modules, hidden
    // ones and links to folders left out.
    void listsFilesToFind();
    // fs/create, fs/rename, fs/copy (a folder with what it holds),
    // fs/remove, fs/trash (to ~/.Trash on macOS); a name taken refused.
    void makesTheSidebarsOperations();
    // FileTree over a host's folder: its entries; a new file and a rename
    // shown at once and made there; a file made elsewhere shows (watched);
    // Move to Trash.
    void showsARemoteFolder();
    // FileSearch over a host's folder.
    void findsRemoteFiles();
    void cleanup();
};
