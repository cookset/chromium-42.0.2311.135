/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 * Copyright (C) 2009 Joseph Pecoraro
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @constructor
 * @extends {WebInspector.PropertiesSection}
 * @param {!WebInspector.RemoteObject} object
 * @param {?string|!Element=} title
 * @param {string=} subtitle
 * @param {?string=} emptyPlaceholder
 * @param {boolean=} ignoreHasOwnProperty
 * @param {!Array.<!WebInspector.RemoteObjectProperty>=} extraProperties
 */
WebInspector.ObjectPropertiesSection = function(object, title, subtitle, emptyPlaceholder, ignoreHasOwnProperty, extraProperties)
{
    this._emptyPlaceholder = emptyPlaceholder;
    this.object = object;
    this.ignoreHasOwnProperty = ignoreHasOwnProperty;
    this.extraProperties = extraProperties;
    this.editable = true;
    this._skipProto = false;

    WebInspector.PropertiesSection.call(this, title || "", subtitle);
}

/** @const */
WebInspector.ObjectPropertiesSection._arrayLoadThreshold = 100;

WebInspector.ObjectPropertiesSection.prototype = {
    skipProto: function()
    {
        this._skipProto = true;
    },

    enableContextMenu: function()
    {
        this.element.addEventListener("contextmenu", this._contextMenuEventFired.bind(this), false);
    },

    _contextMenuEventFired: function(event)
    {
        var contextMenu = new WebInspector.ContextMenu(event);
        contextMenu.appendApplicableItems(this.object);
        contextMenu.show();
    },

    onpopulate: function()
    {
        this.update();
    },

    update: function()
    {
        if (this.object.arrayLength() > WebInspector.ObjectPropertiesSection._arrayLoadThreshold) {
            this.propertiesTreeOutline.removeChildren();
            WebInspector.ArrayGroupingTreeElement._populateArray(this.propertiesTreeOutline, this.object, 0, this.object.arrayLength() - 1);
            return;
        }

        /**
         * @param {?Array.<!WebInspector.RemoteObjectProperty>} properties
         * @param {?Array.<!WebInspector.RemoteObjectProperty>} internalProperties
         * @this {WebInspector.ObjectPropertiesSection}
         */
        function callback(properties, internalProperties)
        {
            if (!properties)
                return;
            this.updateProperties(properties, internalProperties);
        }

        WebInspector.RemoteObject.loadFromObject(this.object, !!this.ignoreHasOwnProperty, callback.bind(this));
    },

    updateProperties: function(properties, internalProperties)
    {
        if (this.extraProperties) {
            for (var i = 0; i < this.extraProperties.length; ++i)
                properties.push(this.extraProperties[i]);
        }

        this.propertiesTreeOutline.removeChildren();

        WebInspector.ObjectPropertyTreeElement.populateWithProperties(this.propertiesTreeOutline,
            properties, internalProperties,
            this._skipProto, this.object, this._emptyPlaceholder);

        this.propertiesForTest = properties;
    },

    __proto__: WebInspector.PropertiesSection.prototype
}

/**
 * @param {!WebInspector.RemoteObjectProperty} propertyA
 * @param {!WebInspector.RemoteObjectProperty} propertyB
 * @return {number}
 */
WebInspector.ObjectPropertiesSection.CompareProperties = function(propertyA, propertyB)
{
    var a = propertyA.name;
    var b = propertyB.name;
    if (a === "__proto__")
        return 1;
    if (b === "__proto__")
        return -1;
    if (propertyA.symbol && !propertyB.symbol)
        return 1;
    if (propertyB.symbol && !propertyA.symbol)
        return -1;
    return String.naturalOrderComparator(a, b);
}

/**
 * @constructor
 * @extends {TreeElement}
 * @param {!WebInspector.RemoteObjectProperty} property
 */
WebInspector.ObjectPropertyTreeElement = function(property)
{
    this.property = property;

    // Pass an empty title, the title gets made later in onattach.
    TreeElement.call(this, "", null, false);
    this.toggleOnClick = true;
    this.selectable = false;
}

WebInspector.ObjectPropertyTreeElement.prototype = {
    onpopulate: function()
    {
        var propertyValue = /** @type {!WebInspector.RemoteObject} */ (this.property.value);
        console.assert(propertyValue);
        var section = this.treeOutline ? this.treeOutline.section : null;
        var skipProto = section ? section._skipProto : true;
        WebInspector.ObjectPropertyTreeElement._populate(this, propertyValue, skipProto);
    },

    /**
     * @override
     * @return {boolean}
     */
    ondblclick: function(event)
    {
        var editableElement = this.valueElement;
        if ((this.property.writable || this.property.setter) && event.target.isSelfOrDescendant(editableElement))
            this.startEditing(event);
        return false;
    },

    /**
     * @override
     */
    onattach: function()
    {
        this.update();
    },

    update: function()
    {
        this.nameElement = WebInspector.ObjectPropertiesSection.createNameElement(this.property.name);
        if (!this.property.enumerable)
            this.nameElement.classList.add("dimmed");
        if (this.property.isAccessorProperty())
            this.nameElement.classList.add("properties-accessor-property-name");
        if (this.property.symbol)
            this.nameElement.addEventListener("contextmenu", this._contextMenuFired.bind(this, this.property.symbol), false);

        if (this.property.value) {
            this.valueElement = WebInspector.ObjectPropertiesSection.createValueElement(this.property.value, this.property.wasThrown, this.listItemElement);
            this.valueElement.addEventListener("contextmenu", this._contextMenuFired.bind(this, this.property.value), false);
            this.hasChildren = this.property.value.hasChildren && !this.property.wasThrown;
        } else if (this.property.getter) {
            this.valueElement = WebInspector.ObjectPropertyTreeElement.createRemoteObjectAccessorPropertySpan(this.property.parentObject, [this.property.name], this._onInvokeGetterClick.bind(this));
        } else {
            this.valueElement = createElementWithClass("span", "console-formatted-undefined");
            this.valueElement.textContent = WebInspector.UIString("<unreadable>");
            this.valueElement.title = WebInspector.UIString("No property getter");
        }

        var separatorElement = createElementWithClass("span", "separator");
        separatorElement.textContent = ": ";

        this.listItemElement.removeChildren();
        this.listItemElement.appendChildren(this.nameElement, separatorElement, this.valueElement);
    },

    _contextMenuFired: function(value, event)
    {
        var contextMenu = new WebInspector.ContextMenu(event);
        contextMenu.appendApplicableItems(value);
        contextMenu.show();
    },

    updateSiblings: function()
    {
        if (this.parent.root)
            this.treeOutline.section.update();
        else
            this.parent.invalidateChildren();
    },

    /**
     * @param {!Event=} event
     */
    startEditing: function(event)
    {
        var valueToEdit = (typeof this.valueElement._originalTextContent === "string") ? this.valueElement._originalTextContent : undefined;

        if (WebInspector.isBeingEdited(this.valueElement) || !this.treeOutline.section.editable || this._readOnly)
            return;

        // Edit original source.
        if (typeof valueToEdit !== "undefined")
            this.valueElement.setTextContentTruncatedIfNeeded(valueToEdit, WebInspector.UIString("<string is too large to edit>"));

        var context = { expanded: this.expanded, previousContent: this.valueElement.textContent };

        // Lie about our children to prevent expanding on double click and to collapse subproperties.
        this.hasChildren = false;

        this.listItemElement.classList.add("editing-sub-part");

        this._prompt = new WebInspector.ObjectPropertyPrompt();

        /**
         * @this {WebInspector.ObjectPropertyTreeElement}
         */
        function blurListener()
        {
            this.editingCommitted(null, this.valueElement.textContent, context.previousContent, context);
        }

        var proxyElement = this._prompt.attachAndStartEditing(this.valueElement, blurListener.bind(this));
        this.listItemElement.getComponentSelection().setBaseAndExtent(this.valueElement, 0, this.valueElement, 1);
        proxyElement.addEventListener("keydown", this._promptKeyDown.bind(this, context), false);
    },

    /**
     * @return {boolean}
     */
    isEditing: function()
    {
        return !!this._prompt;
    },

    editingEnded: function(context)
    {
        this._prompt.detach();
        delete this._prompt;

        this.listItemElement.scrollLeft = 0;
        this.listItemElement.classList.remove("editing-sub-part");
        if (context.expanded)
            this.expand();
    },

    editingCancelled: function(element, context)
    {
        this.editingEnded(context);
        this.update();
    },

    editingCommitted: function(element, userInput, previousContent, context)
    {
        if (userInput === previousContent) {
            this.editingCancelled(element, context); // nothing changed, so cancel
            return;
        }

        this.editingEnded(context);
        this.applyExpression(userInput);
    },

    _promptKeyDown: function(context, event)
    {
        if (isEnterKey(event)) {
            event.consume(true);
            this.editingCommitted(null, this.valueElement.textContent, context.previousContent, context);
            return;
        }
        if (event.keyIdentifier === "U+001B") { // Esc
            event.consume();
            this.editingCancelled(null, context);
            return;
        }
    },

    /**
     * @param {string} expression
     */
    applyExpression: function(expression)
    {
        var property = WebInspector.RemoteObject.toCallArgument(this.property.symbol || this.property.name);
        expression = expression.trim();
        if (expression)
            this.property.parentObject.setPropertyValue(property, expression, callback.bind(this));
        else
            this.property.parentObject.deleteProperty(property, callback.bind(this));

        /**
         * @param {?Protocol.Error} error
         * @this {WebInspector.ObjectPropertyTreeElement}
         */
        function callback(error)
        {
            if (error) {
                this.update();
                return;
            }

            if (!expression) {
                // The property was deleted, so remove this tree element.
                this.parent.removeChild(this);
            } else {
                // Call updateSiblings since their value might be based on the value that just changed.
                this.updateSiblings();
            }
        };
    },

    /**
     * @override
     * @return {*}
     */
    elementIdentity: function()
    {
        return this.propertyPath();
    },

    /**
     * @return {string|undefined}
     */
    propertyPath: function()
    {
        if (this._cachedPropertyPath)
            return this._cachedPropertyPath;

        var current = this;
        var result;

        do {
            if (current.property) {
                if (result)
                    result = current.property.name + "." + result;
                else
                    result = current.property.name;
            }
            current = current.parent;
        } while (current && !current.root);

        this._cachedPropertyPath = result;
        return result;
    },

    /**
     * @param {?WebInspector.RemoteObject} result
     * @param {boolean=} wasThrown
     */
    _onInvokeGetterClick: function(result, wasThrown)
    {
        if (!result)
            return;
        this.property.value = result;
        this.property.wasThrown = wasThrown;

        this.update();
        this.invalidateChildren();
    },

    __proto__: TreeElement.prototype
}

/**
 * @param {!TreeElement} treeElement
 * @param {!WebInspector.RemoteObject} value
 * @param {boolean} skipProto
 * @param {string=} emptyPlaceholder
 */
WebInspector.ObjectPropertyTreeElement._populate = function(treeElement, value, skipProto, emptyPlaceholder) {
    if (value.arrayLength() > WebInspector.ObjectPropertiesSection._arrayLoadThreshold) {
        treeElement.removeChildren();
        WebInspector.ArrayGroupingTreeElement._populateArray(treeElement, value, 0, value.arrayLength() - 1);
        return;
    }

    /**
     * @param {?Array.<!WebInspector.RemoteObjectProperty>} properties
     * @param {?Array.<!WebInspector.RemoteObjectProperty>} internalProperties
     */
    function callback(properties, internalProperties)
    {
        treeElement.removeChildren();
        if (!properties)
            return;
        WebInspector.ObjectPropertyTreeElement.populateWithProperties(treeElement, properties, internalProperties,
            skipProto, value, emptyPlaceholder);
    }

    WebInspector.RemoteObject.loadFromObjectPerProto(value, callback);
}

/**
 * @param {!TreeContainerNode} treeNode
 * @param {!Array.<!WebInspector.RemoteObjectProperty>} properties
 * @param {?Array.<!WebInspector.RemoteObjectProperty>} internalProperties
 * @param {boolean} skipProto
 * @param {?WebInspector.RemoteObject} value
 * @param {?string=} emptyPlaceholder
 */
WebInspector.ObjectPropertyTreeElement.populateWithProperties = function(treeNode, properties, internalProperties, skipProto, value, emptyPlaceholder) {
    properties.sort(WebInspector.ObjectPropertiesSection.CompareProperties);

    for (var i = 0; i < properties.length; ++i) {
        var property = properties[i];
        if (skipProto && property.name === "__proto__")
            continue;
        if (property.isAccessorProperty()) {
            if (property.name !== "__proto__" && property.getter) {
                property.parentObject = value;
                treeNode.appendChild(new WebInspector.ObjectPropertyTreeElement(property));
            }
            if (property.isOwn) {
                if (property.getter) {
                    var getterProperty = new WebInspector.RemoteObjectProperty("get " + property.name, property.getter);
                    getterProperty.parentObject = value;
                    treeNode.appendChild(new WebInspector.ObjectPropertyTreeElement(getterProperty));
                }
                if (property.setter) {
                    var setterProperty = new WebInspector.RemoteObjectProperty("set " + property.name, property.setter);
                    setterProperty.parentObject = value;
                    treeNode.appendChild(new WebInspector.ObjectPropertyTreeElement(setterProperty));
                }
            }
        } else {
            property.parentObject = value;
            treeNode.appendChild(new WebInspector.ObjectPropertyTreeElement(property));
        }
    }
    if (internalProperties) {
        for (var i = 0; i < internalProperties.length; i++) {
            internalProperties[i].parentObject = value;
            treeNode.appendChild(new WebInspector.ObjectPropertyTreeElement(internalProperties[i]));
        }
    }
    if (value && value.type === "function") {
        // Whether function has TargetFunction internal property.
        // This is a simple way to tell that the function is actually a bound function (we are not told).
        // Bound function never has inner scope and doesn't need corresponding UI node.
        var hasTargetFunction = false;

        if (internalProperties) {
            for (var i = 0; i < internalProperties.length; i++) {
                if (internalProperties[i].name == "[[TargetFunction]]") {
                    hasTargetFunction = true;
                    break;
                }
            }
        }
        if (!hasTargetFunction)
            treeNode.appendChild(new WebInspector.FunctionScopeMainTreeElement(value));
    }
    if (value && value.type === "object" && (value.subtype === "map" || value.subtype === "set" || value.subtype === "iterator"))
        treeNode.appendChild(new WebInspector.CollectionEntriesMainTreeElement(value));

    WebInspector.ObjectPropertyTreeElement._appendEmptyPlaceholderIfNeeded(treeNode, emptyPlaceholder);
}

/**
 * @param {!TreeContainerNode} treeNode
 * @param {?string=} emptyPlaceholder
 */
WebInspector.ObjectPropertyTreeElement._appendEmptyPlaceholderIfNeeded = function(treeNode, emptyPlaceholder)
{
    if (treeNode.children.length)
        return;
    var title = createElementWithClass("div", "info");
    title.textContent = emptyPlaceholder || WebInspector.UIString("No Properties");
    var infoElement = new TreeElement(title, null, false);
    treeNode.appendChild(infoElement);
}

/**
 * @param {?WebInspector.RemoteObject} object
 * @param {!Array.<string>} propertyPath
 * @param {function(?WebInspector.RemoteObject, boolean=)} callback
 * @return {!Element}
 */
WebInspector.ObjectPropertyTreeElement.createRemoteObjectAccessorPropertySpan = function(object, propertyPath, callback)
{
    var rootElement = createElement("span");
    var element = rootElement.createChild("span");
    element.textContent = WebInspector.UIString("(...)");
    if (!object)
        return rootElement;
    element.classList.add("properties-calculate-value-button");
    element.title = WebInspector.UIString("Invoke property getter");
    element.addEventListener("click", onInvokeGetterClick, false);

    function onInvokeGetterClick(event)
    {
        event.consume();
        object.getProperty(propertyPath, callback);
    }

    return rootElement;
}

/**
 * @constructor
 * @extends {TreeElement}
 * @param {!WebInspector.RemoteObject} remoteObject
 */
WebInspector.FunctionScopeMainTreeElement = function(remoteObject)
{
    TreeElement.call(this, "<function scope>", null, false);
    this.toggleOnClick = true;
    this.selectable = false;
    this._remoteObject = remoteObject;
    this.hasChildren = true;
}

WebInspector.FunctionScopeMainTreeElement.prototype = {
    onpopulate: function()
    {
        /**
         * @param {?WebInspector.DebuggerModel.FunctionDetails} response
         * @this {WebInspector.FunctionScopeMainTreeElement}
         */
        function didGetDetails(response)
        {
            if (!response)
                return;
            this.removeChildren();

            var scopeChain = response.scopeChain || [];
            for (var i = 0; i < scopeChain.length; ++i) {
                var scope = scopeChain[i];
                var title = null;
                var isTrueObject = false;

                switch (scope.type) {
                case DebuggerAgent.ScopeType.Local:
                    // Not really expecting this scope type here.
                    title = WebInspector.UIString("Local");
                    break;
                case DebuggerAgent.ScopeType.Closure:
                    title = WebInspector.UIString("Closure");
                    break;
                case DebuggerAgent.ScopeType.Catch:
                    title = WebInspector.UIString("Catch");
                    break;
                case DebuggerAgent.ScopeType.Block:
                    title = WebInspector.UIString("Block");
                    break;
                case DebuggerAgent.ScopeType.Script:
                    title = WebInspector.UIString("Script");
                    break;
                case DebuggerAgent.ScopeType.With:
                    title = WebInspector.UIString("With Block");
                    isTrueObject = true;
                    break;
                case DebuggerAgent.ScopeType.Global:
                    title = WebInspector.UIString("Global");
                    isTrueObject = true;
                    break;
                default:
                    console.error("Unknown scope type: " + scope.type);
                    continue;
                }

                var runtimeModel = this._remoteObject.target().runtimeModel;
                if (isTrueObject) {
                    var remoteObject = runtimeModel.createRemoteObject(scope.object);
                    var property = new WebInspector.RemoteObjectProperty(title, remoteObject);
                    property.writable = false;
                    property.parentObject = null;
                    this.appendChild(new WebInspector.ObjectPropertyTreeElement(property));
                } else {
                    var scopeRef = new WebInspector.ScopeRef(i, undefined, this._remoteObject.objectId);
                    var remoteObject = runtimeModel.createScopeRemoteObject(scope.object, scopeRef);
                    var scopeTreeElement = new WebInspector.ScopeTreeElement(title, remoteObject);
                    this.appendChild(scopeTreeElement);
                }
            }

            WebInspector.ObjectPropertyTreeElement._appendEmptyPlaceholderIfNeeded(this, WebInspector.UIString("No Scopes"));
        }

        this._remoteObject.functionDetails(didGetDetails.bind(this));
    },

    __proto__: TreeElement.prototype
}

/**
 * @constructor
 * @extends {TreeElement}
 * @param {!WebInspector.RemoteObject} remoteObject
 */
WebInspector.CollectionEntriesMainTreeElement = function(remoteObject)
{
    TreeElement.call(this, "<entries>", null, false);
    this.toggleOnClick = true;
    this.selectable = false;
    this._remoteObject = remoteObject;
    this.hasChildren = true;
    this.expand();
}

WebInspector.CollectionEntriesMainTreeElement.prototype = {
    onpopulate: function()
    {
        /**
         * @param {?Array.<!DebuggerAgent.CollectionEntry>} entries
         * @this {WebInspector.CollectionEntriesMainTreeElement}
         */
        function didGetCollectionEntries(entries)
        {
            if (!entries)
                return;
            this.removeChildren();

            var entriesLocalObject = [];
            var runtimeModel = this._remoteObject.target().runtimeModel;
            for (var i = 0; i < entries.length; ++i) {
                var entry = entries[i];
                if (entry.key) {
                    entriesLocalObject.push(new WebInspector.MapEntryLocalJSONObject({
                        key: runtimeModel.createRemoteObject(entry.key),
                        value: runtimeModel.createRemoteObject(entry.value)
                    }));
                } else {
                    entriesLocalObject.push(runtimeModel.createRemoteObject(entry.value));
                }
            }
            WebInspector.ObjectPropertyTreeElement._populate(this, WebInspector.RemoteObject.fromLocalObject(entriesLocalObject), true, WebInspector.UIString("No Entries"));
            this.title = "<entries>[" + entriesLocalObject.length + "]";
        }

        this._remoteObject.collectionEntries(didGetCollectionEntries.bind(this));
    },

    __proto__: TreeElement.prototype
}

/**
 * @constructor
 * @extends {TreeElement}
 * @param {string} title
 * @param {!WebInspector.RemoteObject} remoteObject
 */
WebInspector.ScopeTreeElement = function(title, remoteObject)
{
    TreeElement.call(this, title, null, false);
    this.toggleOnClick = true;
    this.selectable = false;
    this._remoteObject = remoteObject;
    this.hasChildren = true;
}

WebInspector.ScopeTreeElement.prototype = {
    onpopulate: function()
    {
        WebInspector.ObjectPropertyTreeElement._populate(this, this._remoteObject, false);
    },

    __proto__: TreeElement.prototype
}

/**
 * @constructor
 * @extends {TreeElement}
 * @param {!WebInspector.RemoteObject} object
 * @param {number} fromIndex
 * @param {number} toIndex
 * @param {number} propertyCount
 */
WebInspector.ArrayGroupingTreeElement = function(object, fromIndex, toIndex, propertyCount)
{
    TreeElement.call(this, String.sprintf("[%d \u2026 %d]", fromIndex, toIndex), undefined, true);
    this.toggleOnClick = true;
    this.selectable = false;
    this._fromIndex = fromIndex;
    this._toIndex = toIndex;
    this._object = object;
    this._readOnly = true;
    this._propertyCount = propertyCount;
    this._populated = false;
}

WebInspector.ArrayGroupingTreeElement._bucketThreshold = 100;
WebInspector.ArrayGroupingTreeElement._sparseIterationThreshold = 250000;
WebInspector.ArrayGroupingTreeElement._getOwnPropertyNamesThreshold = 500000;

/**
 * @param {!TreeContainerNode} treeNode
 * @param {!WebInspector.RemoteObject} object
 * @param {number} fromIndex
 * @param {number} toIndex
 */
WebInspector.ArrayGroupingTreeElement._populateArray = function(treeNode, object, fromIndex, toIndex)
{
    WebInspector.ArrayGroupingTreeElement._populateRanges(treeNode, object, fromIndex, toIndex, true);
}

/**
 * @param {!TreeContainerNode} treeNode
 * @param {!WebInspector.RemoteObject} object
 * @param {number} fromIndex
 * @param {number} toIndex
 * @param {boolean} topLevel
 * @this {WebInspector.ArrayGroupingTreeElement}
 */
WebInspector.ArrayGroupingTreeElement._populateRanges = function(treeNode, object, fromIndex, toIndex, topLevel)
{
    object.callFunctionJSON(packRanges, [
        { value: fromIndex },
        { value: toIndex },
        { value: WebInspector.ArrayGroupingTreeElement._bucketThreshold },
        { value: WebInspector.ArrayGroupingTreeElement._sparseIterationThreshold },
        { value: WebInspector.ArrayGroupingTreeElement._getOwnPropertyNamesThreshold }
    ], callback);

    /**
     * Note: must declare params as optional.
     * @param {number=} fromIndex
     * @param {number=} toIndex
     * @param {number=} bucketThreshold
     * @param {number=} sparseIterationThreshold
     * @param {number=} getOwnPropertyNamesThreshold
     * @suppressReceiverCheck
     * @this {Object}
     */
    function packRanges(fromIndex, toIndex, bucketThreshold, sparseIterationThreshold, getOwnPropertyNamesThreshold)
    {
        var ownPropertyNames = null;
        var consecutiveRange = (toIndex - fromIndex >= sparseIterationThreshold) && ArrayBuffer.isView(this);
        var skipGetOwnPropertyNames = consecutiveRange && (toIndex - fromIndex >= getOwnPropertyNamesThreshold);

        function* arrayIndexes(object)
        {
            if (toIndex - fromIndex < sparseIterationThreshold) {
                for (var i = fromIndex; i <= toIndex; ++i) {
                    if (i in object)
                        yield i;
                }
            } else {
                ownPropertyNames = ownPropertyNames || Object.getOwnPropertyNames(object);
                for (var i = 0; i < ownPropertyNames.length; ++i) {
                    var name = ownPropertyNames[i];
                    var index = name >>> 0;
                    if (("" + index) === name && fromIndex <= index && index <= toIndex)
                        yield index;
                }
            }
        }

        var count = 0;
        if (consecutiveRange) {
            count = toIndex - fromIndex + 1;
        } else {
            for (var i of arrayIndexes(this))
                ++count;
        }

        var bucketSize = count;
        if (count <= bucketThreshold)
            bucketSize = count;
        else
            bucketSize = Math.pow(bucketThreshold, Math.ceil(Math.log(count) / Math.log(bucketThreshold)) - 1);

        var ranges = [];
        if (consecutiveRange) {
            for (var i = fromIndex; i <= toIndex; i += bucketSize) {
                var groupStart = i;
                var groupEnd = groupStart + bucketSize - 1;
                if (groupEnd > toIndex)
                    groupEnd = toIndex;
                ranges.push([groupStart, groupEnd, groupEnd - groupStart + 1]);
            }
        } else {
            count = 0;
            var groupStart = -1;
            var groupEnd = 0;
            for (var i of arrayIndexes(this)) {
                if (groupStart === -1)
                    groupStart = i;
                groupEnd = i;
                if (++count === bucketSize) {
                    ranges.push([groupStart, groupEnd, count]);
                    count = 0;
                    groupStart = -1;
                }
            }
            if (count > 0)
                ranges.push([groupStart, groupEnd, count]);
        }

        return { ranges: ranges, skipGetOwnPropertyNames: skipGetOwnPropertyNames };
    }

    function callback(result)
    {
        if (!result)
            return;
        var ranges = /** @type {!Array.<!Array.<number>>} */ (result.ranges);
        if (ranges.length == 1) {
            WebInspector.ArrayGroupingTreeElement._populateAsFragment(treeNode, object, ranges[0][0], ranges[0][1]);
        } else {
            for (var i = 0; i < ranges.length; ++i) {
                var fromIndex = ranges[i][0];
                var toIndex = ranges[i][1];
                var count = ranges[i][2];
                if (fromIndex == toIndex)
                    WebInspector.ArrayGroupingTreeElement._populateAsFragment(treeNode, object, fromIndex, toIndex);
                else
                    treeNode.appendChild(new WebInspector.ArrayGroupingTreeElement(object, fromIndex, toIndex, count));
            }
        }
        if (topLevel)
            WebInspector.ArrayGroupingTreeElement._populateNonIndexProperties(treeNode, object, result.skipGetOwnPropertyNames);
    }
}

/**
 * @param {!TreeContainerNode} treeNode
 * @param {!WebInspector.RemoteObject} object
 * @param {number} fromIndex
 * @param {number} toIndex
 * @this {WebInspector.ArrayGroupingTreeElement}
 */
WebInspector.ArrayGroupingTreeElement._populateAsFragment = function(treeNode, object, fromIndex, toIndex)
{
    object.callFunction(buildArrayFragment, [{value: fromIndex}, {value: toIndex}, {value: WebInspector.ArrayGroupingTreeElement._sparseIterationThreshold}], processArrayFragment.bind(this));

    /**
     * @suppressReceiverCheck
     * @this {Object}
     * @param {number=} fromIndex // must declare optional
     * @param {number=} toIndex // must declare optional
     * @param {number=} sparseIterationThreshold // must declare optional
     */
    function buildArrayFragment(fromIndex, toIndex, sparseIterationThreshold)
    {
        var result = Object.create(null);
        if (toIndex - fromIndex < sparseIterationThreshold) {
            for (var i = fromIndex; i <= toIndex; ++i) {
                if (i in this)
                    result[i] = this[i];
            }
        } else {
            var ownPropertyNames = Object.getOwnPropertyNames(this);
            for (var i = 0; i < ownPropertyNames.length; ++i) {
                var name = ownPropertyNames[i];
                var index = name >>> 0;
                if (String(index) === name && fromIndex <= index && index <= toIndex)
                    result[index] = this[index];
            }
        }
        return result;
    }

    /**
     * @param {?WebInspector.RemoteObject} arrayFragment
     * @param {boolean=} wasThrown
     * @this {WebInspector.ArrayGroupingTreeElement}
     */
    function processArrayFragment(arrayFragment, wasThrown)
    {
        if (!arrayFragment || wasThrown)
            return;
        arrayFragment.getAllProperties(false, processProperties.bind(this));
    }

    /** @this {WebInspector.ArrayGroupingTreeElement} */
    function processProperties(properties, internalProperties)
    {
        if (!properties)
            return;

        properties.sort(WebInspector.ObjectPropertiesSection.CompareProperties);
        for (var i = 0; i < properties.length; ++i) {
            properties[i].parentObject = this._object;
            var childTreeElement = new WebInspector.ObjectPropertyTreeElement(properties[i]);
            childTreeElement._readOnly = true;
            treeNode.appendChild(childTreeElement);
        }
    }
}

/**
 * @param {!TreeContainerNode} treeNode
 * @param {!WebInspector.RemoteObject} object
 * @param {boolean} skipGetOwnPropertyNames
 * @this {WebInspector.ArrayGroupingTreeElement}
 */
WebInspector.ArrayGroupingTreeElement._populateNonIndexProperties = function(treeNode, object, skipGetOwnPropertyNames)
{
    object.callFunction(buildObjectFragment, [{value: skipGetOwnPropertyNames}], processObjectFragment.bind(this));

    /**
     * @param {boolean=} skipGetOwnPropertyNames
     * @suppressReceiverCheck
     * @this {Object}
     */
    function buildObjectFragment(skipGetOwnPropertyNames)
    {
        var result = { __proto__: this.__proto__ };
        if (skipGetOwnPropertyNames)
            return result;
        var names = Object.getOwnPropertyNames(this);
        for (var i = 0; i < names.length; ++i) {
            var name = names[i];
            // Array index check according to the ES5-15.4.
            if (String(name >>> 0) === name && name >>> 0 !== 0xffffffff)
                continue;
            var descriptor = Object.getOwnPropertyDescriptor(this, name);
            if (descriptor)
                Object.defineProperty(result, name, descriptor);
        }
        return result;
    }

    /**
     * @param {?WebInspector.RemoteObject} arrayFragment
     * @param {boolean=} wasThrown
     * @this {WebInspector.ArrayGroupingTreeElement}
     */
    function processObjectFragment(arrayFragment, wasThrown)
    {
        if (!arrayFragment || wasThrown)
            return;
        arrayFragment.getOwnProperties(processProperties.bind(this));
    }

    /**
     * @param {?Array.<!WebInspector.RemoteObjectProperty>} properties
     * @param {?Array.<!WebInspector.RemoteObjectProperty>=} internalProperties
     * @this {WebInspector.ArrayGroupingTreeElement}
     */
    function processProperties(properties, internalProperties)
    {
        if (!properties)
            return;
        properties.sort(WebInspector.ObjectPropertiesSection.CompareProperties);
        for (var i = 0; i < properties.length; ++i) {
            properties[i].parentObject = this._object;
            var childTreeElement = new WebInspector.ObjectPropertyTreeElement(properties[i]);
            childTreeElement._readOnly = true;
            treeNode.appendChild(childTreeElement);
        }
    }
}

WebInspector.ArrayGroupingTreeElement.prototype = {
    onpopulate: function()
    {
        if (this._populated)
            return;

        this._populated = true;

        if (this._propertyCount >= WebInspector.ArrayGroupingTreeElement._bucketThreshold) {
            WebInspector.ArrayGroupingTreeElement._populateRanges(this, this._object, this._fromIndex, this._toIndex, false);
            return;
        }
        WebInspector.ArrayGroupingTreeElement._populateAsFragment(this, this._object, this._fromIndex, this._toIndex);
    },

    onattach: function()
    {
        this.listItemElement.classList.add("name");
    },

    __proto__: TreeElement.prototype
}

/**
 * @constructor
 * @extends {WebInspector.TextPrompt}
 */
WebInspector.ObjectPropertyPrompt = function()
{
    WebInspector.TextPrompt.call(this, WebInspector.ExecutionContextSelector.completionsForTextPromptInCurrentContext);
    this.setSuggestBoxEnabled(true);
}

WebInspector.ObjectPropertyPrompt.prototype = {
    __proto__: WebInspector.TextPrompt.prototype
}

/**
 * @param {?string} name
 * @return {!Element}
 */
WebInspector.ObjectPropertiesSection.createNameElement = function(name)
{
    var nameElement = createElementWithClass("span", "name");
    if (/^\s|\s$|^$|\n/.test(name))
        nameElement.createTextChildren("\"", name.replace(/\n/g, "\u21B5"), "\"");
    else
        nameElement.textContent = name;
    return nameElement;
}

/**
 * @param {!WebInspector.RemoteObject} value
 * @param {boolean} wasThrown
 * @param {!Element} parentElement
 * @return {!Element}
 */
WebInspector.ObjectPropertiesSection.createValueElement = function(value, wasThrown, parentElement)
{
    var valueElement = createElementWithClass("span", "value");
    var type = value.type;
    var subtype = value.subtype;
    var description = value.description;
    var prefix;
    var valueText;
    var suffix;
    if (wasThrown) {
        prefix = "[Exception: ";
        valueText = description;
        suffix = "]";
    } else if (type === "string" && typeof description === "string") {
        // Render \n as a nice unicode cr symbol.
        prefix = "\"";
        valueText = description.replace(/\n/g, "\u21B5");
        suffix = "\"";
        valueElement._originalTextContent = "\"" + description + "\"";
    } else if (type === "function" && typeof description === "string") {
        // Render function description until the first \n.
        valueText = /.*/.exec(description)[0].replace(/\s+$/g, "");
        valueElement._originalTextContent = description;
    } else if (type !== "object" || subtype !== "node") {
        valueText = description;
    }
    if (type !== "number" || valueText.indexOf("e") === -1) {
        valueElement.setTextContentTruncatedIfNeeded(valueText || "");
        if (prefix)
            valueElement.insertBefore(createTextNode(prefix), valueElement.firstChild);
        if (suffix)
            valueElement.createTextChild(suffix);
    } else {
        var numberParts = valueText.split("e");
        var mantissa = valueElement.createChild("span", "scientific-notation-mantissa");
        mantissa.textContent = numberParts[0];
        var exponent = valueElement.createChild("span", "scientific-notation-exponent");
        exponent.textContent = "e" + numberParts[1];
        valueElement.classList.add("scientific-notation-number");
        parentElement.classList.add("hbox");
    }

    if (wasThrown)
        valueElement.classList.add("error");
    if (subtype || type)
        valueElement.classList.add("console-formatted-" + (subtype || type));

    if (type === "object" && subtype === "node" && description) {
        WebInspector.DOMPresentationUtils.createSpansForNodeTitle(valueElement, description);
        valueElement.addEventListener("mousemove", mouseMove, false);
        valueElement.addEventListener("mouseleave", mouseLeave, false);
    } else {
        valueElement.title = description || "";
    }

    function mouseMove()
    {
        value.highlightAsDOMNode();
    }

    function mouseLeave()
    {
        value.hideDOMNodeHighlight();
    }

    return valueElement;
}
