#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>

#include "keymap.h"

KeyMap::KeyMap(QObject *parent) : QObject(parent) {}

KeyMap::~KeyMap() {}

void KeyMap::loadKeyMap(const QString &json)
{
    QString errorString;
    QJsonParseError jsonError;
    QJsonDocument jsonDoc;
    QJsonObject rootObj;
    QPair<ActionType, int> switchKey;

    jsonDoc = QJsonDocument::fromJson(json.toUtf8(), &jsonError);

    if (jsonError.error != QJsonParseError::NoError) {
        errorString = QString("json error: %1").arg(jsonError.errorString());
        goto parseError;
    }

    // switchKey
    rootObj = jsonDoc.object();

    if (!checkItemString(rootObj, "switchKey")) {
        errorString = QString("json error: no find switchKey");
        goto parseError;
    }

    switchKey = getItemKey(rootObj, "switchKey");
    if (switchKey.first == AT_INVALID) {
        errorString = QString("json error: switchKey invalid");
        goto parseError;
    }

    m_switchKey.type = switchKey.first;
    m_switchKey.key = switchKey.second;

    // mouseMoveMap
    if (checkItemObject(rootObj, "mouseMoveMap")) {
        QJsonObject mouseMoveMap = getItemObject(rootObj, "mouseMoveMap");
        KeyMapNode keyMapNode;
        keyMapNode.type = KMT_MOUSE_MOVE;

        bool have_speedRatio = false;

        // General speedRatio (for backwards compatibility)
        if (checkItemDouble(mouseMoveMap, "speedRatio")) {
            float ratio = static_cast<float>(getItemDouble(mouseMoveMap, "speedRatio"));
            keyMapNode.data.mouseMove.speedRatio.setX(ratio);
            keyMapNode.data.mouseMove.speedRatio.setY(ratio / 2.25f); // Phone screens are often FHD+
            have_speedRatio = true;
        }

        // Individual X Ratio
        if (checkItemDouble(mouseMoveMap, "speedRatioX")) {
            keyMapNode.data.mouseMove.speedRatio.setX(static_cast<float>(getItemDouble(mouseMoveMap, "speedRatioX")));
            have_speedRatio = true;
        }

        // Individual Y Ratio
        if (checkItemDouble(mouseMoveMap, "speedRatioY")) {
            keyMapNode.data.mouseMove.speedRatio.setY(static_cast<float>(getItemDouble(mouseMoveMap, "speedRatioY")));
            have_speedRatio = true;
        }

        if (!have_speedRatio) {
            errorString = QString("json error: speedRatio setting is missing in mouseMoveMap!");
            goto parseError;
        }

        // Sanity check: No ratio must be lower than 0.001
        if ( ( keyMapNode.data.mouseMove.speedRatio.x() < 0.001f ) || ( keyMapNode.data.mouseMove.speedRatio.x() < 0.001f ) ) {
            errorString = QString("json error: Minimum speedRatio is 0.001");
            goto parseError;
        }

        if (!checkItemObject(mouseMoveMap, "startPos")) {
            errorString = QString("json error: mouseMoveMap on find startPos");
            goto parseError;
        }
        QJsonObject startPos = mouseMoveMap.value("startPos").toObject();
        if (checkItemDouble(startPos, "x")) {
            keyMapNode.data.mouseMove.startPos.setX(getItemDouble(startPos, "x"));
        }
        if (checkItemDouble(startPos, "y")) {
            keyMapNode.data.mouseMove.startPos.setY(getItemDouble(startPos, "y"));
        }

        // small eyes
        if (checkItemObject(mouseMoveMap, "smallEyes")) {
            QJsonObject smallEyes = mouseMoveMap.value("smallEyes").toObject();
            if (!smallEyes.contains("type") || !smallEyes.value("type").isString()) {
                errorString = QString("json error: smallEyes no find node type");
                goto parseError;
            }

            // type just support KMT_CLICK
            KeyMap::KeyMapType type = getItemKeyMapType(smallEyes, "type");
            if (KeyMap::KMT_CLICK != type) {
                errorString = QString("json error: smallEyes just support KMT_CLICK");
                goto parseError;
            }

            // safe check
            if (!checkForClick(smallEyes)) {
                errorString = QString("json error: smallEyes node format error");
                goto parseError;
            }

            QPair<ActionType, int> key = getItemKey(smallEyes, "key");
            if (key.first == AT_INVALID) {
                errorString = QString("json error: keyMapNodes node invalid key: %1").arg(smallEyes.value("key").toString());
                goto parseError;
            }

            keyMapNode.data.mouseMove.smallEyes.type = key.first;
            keyMapNode.data.mouseMove.smallEyes.key = key.second;
            keyMapNode.data.mouseMove.smallEyes.pos = getItemPos(smallEyes, "pos");
        }

        m_idxMouseMove = m_keyMapNodes.size();
        m_keyMapNodes.push_back(keyMapNode);
    }

    // keyMapNodes
    if (rootObj.contains("keyMapNodes") && rootObj.value("keyMapNodes").isArray()) {
        QJsonArray keyMapNodes = rootObj.value("keyMapNodes").toArray();
        QJsonObject node;
        int size = keyMapNodes.size();
        for (int i = 0; i < size; i++) {
            if (!keyMapNodes.at(i).isObject()) {
                errorString = QString("json error: keyMapNodes node must be json object");
                goto parseError;
            }
            node = keyMapNodes.at(i).toObject();
            if (!node.contains("type") || !node.value("type").isString()) {
                errorString = QString("json error: keyMapNodes no find node type");
                goto parseError;
            }

            KeyMap::KeyMapType type = getItemKeyMapType(node, "type");
            switch (type) {
            case KeyMap::KMT_CLICK: {
                // safe check
                if (!checkForClick(node)) {
                    qWarning() << "json error: keyMapNodes node format error";
                    break;
                }
                QPair<ActionType, int> key = getItemKey(node, "key");
                if (key.first == AT_INVALID) {
                    qWarning() << "json error: keyMapNodes node invalid key: " << node.value("key").toString();
                    break;
                }
                KeyMapNode keyMapNode;
                keyMapNode.type = type;
                keyMapNode.data.click.keyNode.type = key.first;
                keyMapNode.data.click.keyNode.key = key.second;
                keyMapNode.data.click.keyNode.pos = getItemPos(node, "pos");
                keyMapNode.data.click.switchMap = getItemBool(node, "switchMap");
                keyMapNode.data.click.keyNode.androidKey = static_cast<AndroidKeycode>(getItemDouble(node, "androidKey"));
                keyMapNode.comment = getItemString(node, "comment");
                m_keyMapNodes.push_back(keyMapNode);
            } break;
            case KeyMap::KMT_CLICK_TWICE: {
                // safe check
                if (!checkForClickTwice(node)) {
                    qWarning() << "json error: keyMapNodes node format error";
                    break;
                }

                QPair<ActionType, int> key = getItemKey(node, "key");
                if (key.first == AT_INVALID) {
                    qWarning() << "json error: keyMapNodes node invalid key: " << node.value("key").toString();
                    break;
                }
                KeyMapNode keyMapNode;
                keyMapNode.type = type;
                keyMapNode.data.click.keyNode.type = key.first;
                keyMapNode.data.click.keyNode.key = key.second;
                keyMapNode.data.click.keyNode.pos = getItemPos(node, "pos");
                keyMapNode.data.click.switchMap = getItemBool(node, "switchMap");
                keyMapNode.data.click.keyNode.androidKey = static_cast<AndroidKeycode>(getItemDouble(node, "androidKey"));
                keyMapNode.comment = getItemString(node, "comment");
                m_keyMapNodes.push_back(keyMapNode);
            } break;
            case KeyMap::KMT_CLICK_MULTI: {
                // safe check
                if (!checkForClickMulti(node)) {
                    qWarning() << "json error: keyMapNodes node format error";
                    break;
                }
                QPair<ActionType, int> key = getItemKey(node, "key");
                if (key.first == AT_INVALID) {
                    qWarning() << "json error: keyMapNodes node invalid key: " << node.value("key").toString();
                    break;
                }
                KeyMapNode keyMapNode;
                keyMapNode.type = type;
                keyMapNode.data.clickMulti.keyNode.type = key.first;
                keyMapNode.data.clickMulti.keyNode.key = key.second;

                QJsonArray clickNodes = node.value("clickNodes").toArray();
                QJsonObject clickNode;
                keyMapNode.data.clickMulti.keyNode.delayClickNodesCount = 0;

                for (int i = 0; i < clickNodes.size(); i++) {
                    if (i >= MAX_DELAY_CLICK_NODES) {
                        qInfo() << "clickNodes too much, up to " << MAX_DELAY_CLICK_NODES;
                        break;
                    }
                    clickNode = clickNodes.at(i).toObject();
                    DelayClickNode delayClickNode;
                    delayClickNode.delay = getItemDouble(clickNode, "delay");
                    delayClickNode.pos = getItemPos(clickNode, "pos");
                    keyMapNode.data.clickMulti.keyNode.delayClickNodes[i] = delayClickNode;
                    keyMapNode.data.clickMulti.keyNode.delayClickNodesCount++;
                }

                keyMapNode.comment = getItemString(node, "comment");
                m_keyMapNodes.push_back(keyMapNode);
            } break;
            case KeyMap::KMT_STEER_WHEEL: {
                // safe check
                if (!checkForSteerWhell(node)) {
                    qWarning() << "json error: keyMapNodes node format error";
                    break;
                }
                QPair<ActionType, int> leftKey = getItemKey(node, "leftKey");
                QPair<ActionType, int> rightKey = getItemKey(node, "rightKey");
                QPair<ActionType, int> upKey = getItemKey(node, "upKey");
                QPair<ActionType, int> downKey = getItemKey(node, "downKey");
                if (leftKey.first == AT_INVALID || rightKey.first == AT_INVALID || upKey.first == AT_INVALID || downKey.first == AT_INVALID) {
                    if (leftKey.first == AT_INVALID) {
                        qWarning() << "json error: keyMapNodes node invalid key: " << node.value("leftKey").toString();
                    }
                    if (rightKey.first == AT_INVALID) {
                        qWarning() << "json error: keyMapNodes node invalid key: " << node.value("rightKey").toString();
                    }
                    if (upKey.first == AT_INVALID) {
                        qWarning() << "json error: keyMapNodes node invalid key: " << node.value("upKey").toString();
                    }
                    if (downKey.first == AT_INVALID) {
                        qWarning() << "json error: keyMapNodes node invalid key: " << node.value("downKey").toString();
                    }
                    break;
                }

                KeyMapNode keyMapNode;
                keyMapNode.type = type;

                keyMapNode.data.steerWheel.left = { leftKey.first, leftKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "leftOffset") };
                keyMapNode.data.steerWheel.right = { rightKey.first, rightKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "rightOffset") };
                keyMapNode.data.steerWheel.up = { upKey.first, upKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "upOffset") };
                keyMapNode.data.steerWheel.down = { downKey.first, downKey.second, QPointF(0, 0), QPointF(0, 0), getItemDouble(node, "downOffset") };

                keyMapNode.data.steerWheel.centerPos = getItemPos(node, "centerPos");
                keyMapNode.comment = getItemString(node, "comment");
                m_idxSteerWheel = m_keyMapNodes.size();
                m_keyMapNodes.push_back(keyMapNode);
            } break;
            case KeyMap::KMT_DRAG: {
                // safe check
                if (!checkForDrag(node)) {
                    qWarning() << "json error: keyMapNodes node format error";
                    break;
                }

                QPair<ActionType, int> key = getItemKey(node, "key");
                if (key.first == AT_INVALID) {
                    qWarning() << "json error: keyMapNodes node invalid key: " << node.value("key").toString();
                    break;
                }
                KeyMapNode keyMapNode;
                keyMapNode.type = type;
                keyMapNode.data.drag.keyNode.type = key.first;
                keyMapNode.data.drag.keyNode.key = key.second;
                keyMapNode.data.drag.keyNode.pos = getItemPos(node, "startPos");
                keyMapNode.data.drag.keyNode.extendPos = getItemPos(node, "endPos");
                // Read optional drag delay parameters
                keyMapNode.data.drag.startDelay = node.contains("startDelay") ? 
                    static_cast<quint32>(getItemDouble(node, "startDelay")) : 0;
                keyMapNode.data.drag.dragSpeed = node.contains("dragSpeed") ? 
                    static_cast<float>(getItemDouble(node, "dragSpeed")) : 1.0f;
                keyMapNode.comment = getItemString(node, "comment");
                m_keyMapNodes.push_back(keyMapNode);
                break;
            }
            case KeyMap::KMT_ANDROID_KEY: {
                // safe check
                if (!checkForAndroidKey(node)) {
                    qWarning() << "json error: keyMapNodes node format error";
                    break;
                }

                QPair<ActionType, int> key = getItemKey(node, "key");
                if (key.first == AT_INVALID) {
                    qWarning() << "json error: keyMapNodes node invalid key: " << node.value("key").toString();
                    break;
                }
                KeyMapNode keyMapNode;
                keyMapNode.type = type;
                keyMapNode.data.androidKey.keyNode.type = key.first;
                keyMapNode.data.androidKey.keyNode.key = key.second;
                keyMapNode.data.androidKey.keyNode.androidKey = static_cast<AndroidKeycode>(getItemDouble(node, "androidKey"));
                keyMapNode.comment = getItemString(node, "comment");
                m_keyMapNodes.push_back(keyMapNode);
            } break;
            default:
                qWarning() << "json error: keyMapNodes invalid node type:" << node.value("type").toString();
                break;
            }
        }
    }
    // this must be called after m_keyMapNodes is stable
    makeReverseMap();
    qInfo() << QString("Script updated, current keymap mode:normal, Press %1 key to switch keymap mode").arg(keyName(m_switchKey.type, m_switchKey.key));

parseError:
    if (!errorString.isEmpty()) {
        qWarning() << errorString;
    }
    return;
}

QString KeyMap::saveKeyMap() const
{
    QJsonObject rootObj;
    rootObj.insert("switchKey", keyName(m_switchKey.type, m_switchKey.key));

    auto posObj = [](const QPointF &pos) {
        QJsonObject obj;
        obj.insert("x", pos.x());
        obj.insert("y", pos.y());
        return obj;
    };

    QJsonArray keyMapNodes;
    for (int i = 0; i < m_keyMapNodes.size(); ++i) {
        const KeyMapNode &node = m_keyMapNodes[i];
        // mouse move nodes are serialized only as the root mouseMoveMap below
        if (KMT_MOUSE_MOVE == node.type) {
            continue;
        }
        const char *typeName = m_metaEnumKeyMapType.valueToKey(node.type);
        if (!typeName) {
            continue;
        }
        QJsonObject nodeObj;
        nodeObj.insert("type", QString::fromUtf8(typeName));
        // VenMirror v1.1.0: conservar el nombre del control en el
        // round-trip (antes se perdía al guardar).
        if (!node.comment.isEmpty()) {
            nodeObj.insert("comment", node.comment);
        }
        switch (node.type) {
        case KMT_CLICK:
        case KMT_CLICK_TWICE: {
            // CLICK_TWICE shares the union storage with CLICK, so both are
            // read from data.click (see loadKeyMap)
            nodeObj.insert("key", keyName(node.data.click.keyNode.type, node.data.click.keyNode.key));
            nodeObj.insert("pos", posObj(node.data.click.keyNode.pos));
            nodeObj.insert("switchMap", node.data.click.switchMap);
            nodeObj.insert("androidKey", static_cast<int>(node.data.click.keyNode.androidKey));
        } break;
        case KMT_CLICK_MULTI: {
            nodeObj.insert("key", keyName(node.data.clickMulti.keyNode.type, node.data.clickMulti.keyNode.key));
            QJsonArray clickNodes;
            for (int j = 0; j < node.data.clickMulti.keyNode.delayClickNodesCount; ++j) {
                const DelayClickNode &delayNode = node.data.clickMulti.keyNode.delayClickNodes[j];
                QJsonObject delayObj;
                delayObj.insert("delay", delayNode.delay);
                delayObj.insert("pos", posObj(delayNode.pos));
                clickNodes.append(delayObj);
            }
            nodeObj.insert("clickNodes", clickNodes);
        } break;
        case KMT_STEER_WHEEL: {
            nodeObj.insert("leftKey", keyName(node.data.steerWheel.left.type, node.data.steerWheel.left.key));
            nodeObj.insert("rightKey", keyName(node.data.steerWheel.right.type, node.data.steerWheel.right.key));
            nodeObj.insert("upKey", keyName(node.data.steerWheel.up.type, node.data.steerWheel.up.key));
            nodeObj.insert("downKey", keyName(node.data.steerWheel.down.type, node.data.steerWheel.down.key));
            nodeObj.insert("leftOffset", node.data.steerWheel.left.extendOffset);
            nodeObj.insert("rightOffset", node.data.steerWheel.right.extendOffset);
            nodeObj.insert("upOffset", node.data.steerWheel.up.extendOffset);
            nodeObj.insert("downOffset", node.data.steerWheel.down.extendOffset);
            nodeObj.insert("centerPos", posObj(node.data.steerWheel.centerPos));
        } break;
        case KMT_DRAG: {
            nodeObj.insert("key", keyName(node.data.drag.keyNode.type, node.data.drag.keyNode.key));
            nodeObj.insert("startPos", posObj(node.data.drag.keyNode.pos));
            nodeObj.insert("endPos", posObj(node.data.drag.keyNode.extendPos));
            nodeObj.insert("startDelay", static_cast<int>(node.data.drag.startDelay));
            nodeObj.insert("dragSpeed", static_cast<double>(node.data.drag.dragSpeed));
        } break;
        case KMT_ANDROID_KEY: {
            nodeObj.insert("key", keyName(node.data.androidKey.keyNode.type, node.data.androidKey.keyNode.key));
            nodeObj.insert("androidKey", static_cast<int>(node.data.androidKey.keyNode.androidKey));
        } break;
        default:
            // KMT_INVALID and unknown types cannot be serialized; skip them
            continue;
        }
        keyMapNodes.append(nodeObj);
    }
    rootObj.insert("keyMapNodes", keyMapNodes);

    if (m_idxMouseMove != -1) {
        const KeyMapNode &mouseMoveNode = m_keyMapNodes[m_idxMouseMove];
        QJsonObject mouseMoveObj;
        mouseMoveObj.insert("startPos", posObj(mouseMoveNode.data.mouseMove.startPos));
        mouseMoveObj.insert("speedRatioX", static_cast<double>(mouseMoveNode.data.mouseMove.speedRatio.x()));
        mouseMoveObj.insert("speedRatioY", static_cast<double>(mouseMoveNode.data.mouseMove.speedRatio.y()));
        if (mouseMoveNode.data.mouseMove.smallEyes.key != Qt::Key_unknown) {
            QJsonObject smallEyesObj;
            smallEyesObj.insert("type", "KMT_CLICK");
            smallEyesObj.insert("key", keyName(mouseMoveNode.data.mouseMove.smallEyes.type, mouseMoveNode.data.mouseMove.smallEyes.key));
            smallEyesObj.insert("pos", posObj(mouseMoveNode.data.mouseMove.smallEyes.pos));
            // loadKeyMap's checkForClick requires a "switchMap" bool on
            // smallEyes, so it is written here (always false: the flag is not
            // stored for smallEyes) to keep save -> load round-tripping
            smallEyesObj.insert("switchMap", false);
            mouseMoveObj.insert("smallEyes", smallEyesObj);
        }
        rootObj.insert("mouseMoveMap", mouseMoveObj);
    }

    QJsonDocument jsonDoc(rootObj);
    return QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Indented));
}

const QVector<KeyMap::KeyMapNode> &KeyMap::keyMapNodes() const
{
    return m_keyMapNodes;
}

void KeyMap::setSwitchKey(ActionType type, int key)
{
    if (AT_INVALID == type || -1 == key) {
        return;
    }
    m_switchKey.type = type;
    m_switchKey.key = key;
}

bool KeyMap::setNodePos(int index, const QPointF &pos)
{
    if (index < 0 || index >= m_keyMapNodes.size()) {
        return false;
    }
    QPointF clampedPos(qBound(0.0, pos.x(), 1.0), qBound(0.0, pos.y(), 1.0));
    KeyMapNode &node = m_keyMapNodes[index];
    switch (node.type) {
    case KMT_CLICK:
    case KMT_CLICK_TWICE:
        node.data.click.keyNode.pos = clampedPos;
        break;
    case KMT_CLICK_MULTI: {
        if (node.data.clickMulti.keyNode.delayClickNodesCount <= 0) {
            return false;
        }
        QPointF delta = clampedPos - node.data.clickMulti.keyNode.delayClickNodes[0].pos;
        for (int i = 0; i < node.data.clickMulti.keyNode.delayClickNodesCount; ++i) {
            node.data.clickMulti.keyNode.delayClickNodes[i].pos += delta;
        }
    } break;
    case KMT_STEER_WHEEL:
        node.data.steerWheel.centerPos = clampedPos;
        break;
    case KMT_DRAG: {
        QPointF delta = clampedPos - node.data.drag.keyNode.pos;
        node.data.drag.keyNode.pos = clampedPos;
        node.data.drag.keyNode.extendPos += delta;
    } break;
    case KMT_MOUSE_MOVE:
        node.data.mouseMove.startPos = clampedPos;
        break;
    case KMT_ANDROID_KEY:
    default:
        return false;
    }
    return true;
}

bool KeyMap::setNodeKey(int index, ActionType type, int key)
{
    if (index < 0 || index >= m_keyMapNodes.size()) {
        return false;
    }
    if (AT_INVALID == type || -1 == key) {
        return false;
    }
    // a node must never steal the mode-switch key
    if (type == m_switchKey.type && key == m_switchKey.key) {
        return false;
    }
    KeyMapNode &node = m_keyMapNodes[index];
    switch (node.type) {
    case KMT_CLICK:
    case KMT_CLICK_TWICE:
        node.data.click.keyNode.type = type;
        node.data.click.keyNode.key = key;
        break;
    case KMT_CLICK_MULTI:
        node.data.clickMulti.keyNode.type = type;
        node.data.clickMulti.keyNode.key = key;
        break;
    case KMT_DRAG:
        node.data.drag.keyNode.type = type;
        node.data.drag.keyNode.key = key;
        break;
    case KMT_ANDROID_KEY:
        node.data.androidKey.keyNode.type = type;
        node.data.androidKey.keyNode.key = key;
        break;
    case KMT_STEER_WHEEL:
    case KMT_MOUSE_MOVE:
    default:
        return false;
    }
    makeReverseMap();
    return true;
}

bool KeyMap::setSteerWheelKeys(int index, int up, int down, int left, int right)
{
    if (index < 0 || index >= m_keyMapNodes.size()) {
        return false;
    }
    KeyMapNode &node = m_keyMapNodes[index];
    if (KMT_STEER_WHEEL != node.type) {
        return false;
    }
    // Las 4 teclas del joystick son siempre de teclado.
    int keys[4] = { up, down, left, right };
    for (int i = 0; i < 4; ++i) {
        if (-1 == keys[i] || Qt::Key_unknown == keys[i]) {
            return false;
        }
        // Ninguna dirección puede robar la tecla de cambio de modo.
        if (AT_KEY == m_switchKey.type && keys[i] == m_switchKey.key) {
            return false;
        }
        for (int j = i + 1; j < 4; ++j) {
            if (keys[i] == keys[j]) {
                return false;
            }
        }
    }
    node.data.steerWheel.up.type = AT_KEY;
    node.data.steerWheel.up.key = up;
    node.data.steerWheel.down.type = AT_KEY;
    node.data.steerWheel.down.key = down;
    node.data.steerWheel.left.type = AT_KEY;
    node.data.steerWheel.left.key = left;
    node.data.steerWheel.right.type = AT_KEY;
    node.data.steerWheel.right.key = right;
    makeReverseMap();
    return true;
}

bool KeyMap::addNode(const KeyMapNode &node)
{
    if (KMT_INVALID == node.type) {
        return false;
    }
    // only one mouseMoveMap is allowed: replace the existing one instead of
    // appending a second
    if (KMT_MOUSE_MOVE == node.type && -1 != m_idxMouseMove) {
        m_keyMapNodes[m_idxMouseMove] = node;
        makeReverseMap();
        return true;
    }
    if (KMT_STEER_WHEEL == node.type) {
        m_idxSteerWheel = m_keyMapNodes.size();
    } else if (KMT_MOUSE_MOVE == node.type) {
        m_idxMouseMove = m_keyMapNodes.size();
    }
    m_keyMapNodes.push_back(node);
    makeReverseMap();
    return true;
}

bool KeyMap::removeNode(int index)
{
    if (index < 0 || index >= m_keyMapNodes.size()) {
        return false;
    }
    m_keyMapNodes.removeAt(index);
    m_idxSteerWheel = -1;
    m_idxMouseMove = -1;
    for (int i = 0; i < m_keyMapNodes.size(); ++i) {
        if (KMT_STEER_WHEEL == m_keyMapNodes[i].type) {
            m_idxSteerWheel = i;
        } else if (KMT_MOUSE_MOVE == m_keyMapNodes[i].type) {
            m_idxMouseMove = i;
        }
    }
    makeReverseMap();
    return true;
}

bool KeyMap::setMouseMoveMap(const KeyMapNode &node)
{
    if (KMT_MOUSE_MOVE != node.type) {
        return false;
    }
    if (-1 != m_idxMouseMove) {
        m_keyMapNodes[m_idxMouseMove] = node;
    } else {
        m_idxMouseMove = m_keyMapNodes.size();
        m_keyMapNodes.push_back(node);
    }
    makeReverseMap();
    return true;
}

bool KeyMap::removeMouseMoveMap()
{
    if (-1 == m_idxMouseMove) {
        return false;
    }
    m_keyMapNodes.removeAt(m_idxMouseMove);
    m_idxMouseMove = -1;
    // removing a node shifts the indices after it; recompute the steer wheel
    m_idxSteerWheel = -1;
    for (int i = 0; i < m_keyMapNodes.size(); ++i) {
        if (KMT_STEER_WHEEL == m_keyMapNodes[i].type) {
            m_idxSteerWheel = i;
        }
    }
    makeReverseMap();
    return true;
}

QString KeyMap::keyName(ActionType type, int key) const
{
    const char *name = Q_NULLPTR;
    if (AT_KEY == type) {
        name = m_metaEnumKey.valueToKey(key);
    } else if (AT_MOUSE == type) {
        name = m_metaEnumMouseButtons.valueToKey(key);
    } else {
        return QString();
    }
    if (!name) {
        return QString();
    }
    return QString::fromUtf8(name);
}

const KeyMap::KeyMapNode &KeyMap::getKeyMapNode(int key)
{
    auto p = m_rmapKey.value(key, &m_invalidNode);
    if (p == &m_invalidNode) {
        return *m_rmapMouse.value(key, &m_invalidNode);
    }
    return *p;
}

const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeKey(int key)
{
    return *m_rmapKey.value(key, &m_invalidNode);
}

const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeMouse(int key)
{
    return *m_rmapMouse.value(key, &m_invalidNode);
}

bool KeyMap::isSwitchOnKeyboard()
{
    return m_switchKey.type == AT_KEY;
}

int KeyMap::getSwitchKey()
{
    return m_switchKey.key;
}

const KeyMap::KeyMapNode &KeyMap::getMouseMoveMap()
{
    return m_keyMapNodes[m_idxMouseMove];
}

bool KeyMap::isValidMouseMoveMap()
{
    return m_idxMouseMove != -1;
}

bool KeyMap::isValidSteerWheelMap()
{
    return m_idxSteerWheel != -1;
}

void KeyMap::makeReverseMap()
{
    m_rmapKey.clear();
    m_rmapMouse.clear();
    for (int i = 0; i < m_keyMapNodes.size(); ++i) {
        auto &node = m_keyMapNodes[i];
        switch (node.type) {
        case KMT_CLICK: {
            QMultiHash<int, KeyMapNode *> &m = node.data.click.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            m.insert(node.data.click.keyNode.key, &node);
        } break;
        case KMT_CLICK_TWICE: {
            QMultiHash<int, KeyMapNode *> &m = node.data.clickTwice.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            m.insert(node.data.clickTwice.keyNode.key, &node);
        } break;
        case KMT_CLICK_MULTI: {
            QMultiHash<int, KeyMapNode *> &m = node.data.clickMulti.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            m.insert(node.data.clickMulti.keyNode.key, &node);
        } break;
        case KMT_STEER_WHEEL: {
            QMultiHash<int, KeyMapNode *> &ml = node.data.steerWheel.left.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            ml.insert(node.data.steerWheel.left.key, &node);
            QMultiHash<int, KeyMapNode *> &mr = node.data.steerWheel.right.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            mr.insert(node.data.steerWheel.right.key, &node);
            QMultiHash<int, KeyMapNode *> &mu = node.data.steerWheel.up.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            mu.insert(node.data.steerWheel.up.key, &node);
            QMultiHash<int, KeyMapNode *> &md = node.data.steerWheel.down.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            md.insert(node.data.steerWheel.down.key, &node);
        } break;
        case KMT_DRAG: {
            QMultiHash<int, KeyMapNode *> &m = node.data.drag.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            m.insert(node.data.drag.keyNode.key, &node);
        } break;
        case KMT_ANDROID_KEY: {
            QMultiHash<int, KeyMapNode *> &m = node.data.androidKey.keyNode.type == AT_KEY ? m_rmapKey : m_rmapMouse;
            m.insert(node.data.androidKey.keyNode.key, &node);
        } break;
        default:
            break;
        }
    }
}

QString KeyMap::getItemString(const QJsonObject &node, const QString &name)
{
    return node.value(name).toString();
}

double KeyMap::getItemDouble(const QJsonObject &node, const QString &name)
{
    return node.value(name).toDouble();
}

bool KeyMap::getItemBool(const QJsonObject &node, const QString &name)
{
    return node.value(name).toBool(false);
}

QJsonObject KeyMap::getItemObject(const QJsonObject &node, const QString &name)
{
    return node.value(name).toObject();
}

QPointF KeyMap::getItemPos(const QJsonObject &node, const QString &name)
{
    QJsonObject pos = node.value(name).toObject();
    return QPointF(pos.value("x").toDouble(), pos.value("y").toDouble());
}

QPair<KeyMap::ActionType, int> KeyMap::getItemKey(const QJsonObject &node, const QString &name)
{
    QString value = getItemString(node, name);
    int key = m_metaEnumKey.keyToValue(value.toStdString().c_str());
    int btn = m_metaEnumMouseButtons.keyToValue(value.toStdString().c_str());
    if (key == -1 && btn == -1) {
        return { AT_INVALID, -1 };
    } else if (key != -1) {
        return { AT_KEY, key };
    } else {
        return { AT_MOUSE, btn };
    }
}

KeyMap::KeyMapType KeyMap::getItemKeyMapType(const QJsonObject &node, const QString &name)
{
    QString value = getItemString(node, name);
    return static_cast<KeyMap::KeyMapType>(m_metaEnumKeyMapType.keyToValue(value.toStdString().c_str()));
}

bool KeyMap::checkItemString(const QJsonObject &node, const QString &name)
{
    return node.contains(name) && node.value(name).isString();
}

bool KeyMap::checkItemDouble(const QJsonObject &node, const QString &name)
{
    return node.contains(name) && node.value(name).isDouble();
}

bool KeyMap::checkItemBool(const QJsonObject &node, const QString &name)
{
    return node.contains(name) && node.value(name).isBool();
}

bool KeyMap::checkItemObject(const QJsonObject &node, const QString &name)
{
    return node.contains(name) && node.value(name).isObject();
}

bool KeyMap::checkItemPos(const QJsonObject &node, const QString &name)
{
    if (node.contains(name) && node.value(name).isObject()) {
        QJsonObject pos = node.value(name).toObject();
        return pos.contains("x") && pos.value("x").isDouble() && pos.contains("y") && pos.value("y").isDouble();
    }
    return false;
}

bool KeyMap::checkForClick(const QJsonObject &node)
{
    return checkForClickTwice(node) && checkItemBool(node, "switchMap");
}

bool KeyMap::checkForClickMulti(const QJsonObject &node)
{
    bool ret = true;

    if (!node.contains("clickNodes") || !node.value("clickNodes").isArray()) {
        qWarning("json error: no find clickNodes");
        return false;
    }

    QJsonArray clickNodes = node.value("clickNodes").toArray();
    QJsonObject clickNode;
    int size = clickNodes.size();
    if (0 == size) {
        qWarning("json error: clickNodes is empty");
        return false;
    }

    for (int i = 0; i < size; i++) {
        if (!clickNodes.at(i).isObject()) {
            qWarning("json error: clickNodes node must be json object");
            ret = false;
            break;
        }

        clickNode = clickNodes.at(i).toObject();
        if (!checkForDelayClickNode(clickNode)) {
            ret = false;
            break;
        }
    }

    return ret;
}

bool KeyMap::checkForDelayClickNode(const QJsonObject &node)
{
    return checkItemPos(node, "pos") && checkItemDouble(node, "delay");
}

bool KeyMap::checkForClickTwice(const QJsonObject &node)
{
    return checkItemString(node, "key") && checkItemPos(node, "pos");
}

bool KeyMap::checkForAndroidKey(const QJsonObject &node)
{
    return checkItemString(node, "key") && checkItemDouble(node, "androidKey");
}

bool KeyMap::checkForSteerWhell(const QJsonObject &node)
{
    return checkItemString(node, "leftKey") && checkItemString(node, "rightKey") && checkItemString(node, "upKey") && checkItemString(node, "downKey")
           && checkItemDouble(node, "leftOffset") && checkItemDouble(node, "rightOffset") && checkItemDouble(node, "upOffset")
           && checkItemDouble(node, "downOffset") && checkItemPos(node, "centerPos");
}

bool KeyMap::checkForDrag(const QJsonObject &node)
{
    return checkItemString(node, "key") && checkItemPos(node, "startPos") && checkItemPos(node, "endPos");
}
