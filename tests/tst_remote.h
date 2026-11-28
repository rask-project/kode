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
    void cleanup();
};
