import firebase from 'firebase/app';
import 'firebase/auth';
import 'firebase/firestore';

const firebaseConfig = {
    // Your Firebase configuration
    // Get this from your Firebase project settings
    apiKey: "YOUR_API_KEY",
    authDomain: "your-app.firebaseapp.com",
    projectId: "your-app-id",
    storageBucket: "your-app.appspot.com",
    messagingSenderId: "your-messaging-sender-id",
    appId: "your-app-id"
};

// Initialize Firebase
if (!firebase.apps.length) {
    firebase.initializeApp(firebaseConfig);
}

export const auth = firebase.auth();
export const firestore = firebase.firestore();

// Get current user token
export const getCurrentUserToken = async () => {
    const user = auth.currentUser;
    if (user) {
        return user.getIdToken(true);
    }
    return null;
};

// Database functions
export const getAvailablePeers = async () => {
    const snapshot = await firestore.collection('peers').where('online', '==', true).get();
    return snapshot.docs.map(doc => ({
        id: doc.id,
        ...doc.data()
    }));
};

export const updatePeerStatus = async (peerId, isOnline) => {
    const userId = auth.currentUser?.uid;
    if (!userId) return;

    await firestore.collection('peers').doc(peerId).set({
        online: isOnline,
        lastSeen: firebase.firestore.FieldValue.serverTimestamp()
    }, { merge: true });
};

export const updateStreamConfig = async (peerId, config) => {
    await firestore.collection('peers').doc(peerId).collection('configs').add({
        ...config,
        timestamp: firebase.firestore.FieldValue.serverTimestamp(),
        sender: auth.currentUser?.uid
    });
};

export const listenToStreamRequests = (peerId, callback) => {
    return firestore.collection('peers').doc(peerId).collection('requests')
        .orderBy('timestamp', 'desc')
        .limit(1)
        .onSnapshot(snapshot => {
            snapshot.docChanges().forEach(change => {
                if (change.type === 'added') {
                    callback(change.doc.data());
                }
            });
        });
};

export default firebase;
